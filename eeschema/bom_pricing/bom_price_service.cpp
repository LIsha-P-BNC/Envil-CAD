/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "bom_price_service.h"

#include "bom_pricing_defs.h"
#include "currency_converter.h"
#include "element14_price_source.h"
#include "findchips_price_source.h"
#include "lcsc_price_source.h"
#include "price_cache.h"
#include "price_source.h"

#include <wx/tokenzr.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>

namespace BOM_PRICING
{

namespace
{

wxString formatInr( double aPrice )
{
    // Locale-independent: the exports and the XLSX writer's numeric-cell detection both need
    // a plain C-locale decimal point.
    return wxString::FromCDouble( aPrice, 2 );
}


std::vector<std::unique_ptr<PRICE_SOURCE>> makeSources( const wxString& aSourceIds )
{
    std::vector<std::unique_ptr<PRICE_SOURCE>> sources;
    wxStringTokenizer tokenizer( aSourceIds.Lower(), wxS( ", " ), wxTOKEN_STRTOK );

    while( tokenizer.HasMoreTokens() )
    {
        wxString id = tokenizer.GetNextToken();

        if( id == wxS( "lcsc" ) )
            sources.push_back( std::make_unique<LCSC_PRICE_SOURCE>() );
        else if( id == wxS( "findchips" ) )
            sources.push_back( std::make_unique<FINDCHIPS_PRICE_SOURCE>() );
        else if( id == wxS( "element14_in" ) || id == wxS( "element14" ) )
            sources.push_back( std::make_unique<ELEMENT14_PRICE_SOURCE>() );

        // Unknown ids are ignored: a stale settings string must not break pricing.
    }

    return sources;
}

} // namespace


wxString BOM_PRICE_SERVICE::NormalizeMpn( const wxString& aMpn )
{
    wxString mpn = aMpn;
    mpn.Trim( true ).Trim( false );
    return mpn.Upper();
}


std::map<wxString, wxString> BOM_PRICE_SERVICE::GetPrices( const std::vector<PART_REQUEST>& aParts,
                                                           const OPTIONS& aOptions,
                                                           const std::function<bool()>& aCancelled )
{
    std::map<wxString, wxString> result;

    try
    {
        // Dedupe on the normalized MPN, keeping the largest quantity seen: the ladder price
        // for the biggest need, and one network round per part no matter how many rows.
        std::map<wxString, int> wanted;

        for( const PART_REQUEST& part : aParts )
        {
            wxString key = NormalizeMpn( part.mpn );

            if( key.IsEmpty() )
                continue;

            int& qty = wanted[key];
            qty = std::max( qty, std::max( part.qty, 1 ) );
        }

        if( wanted.empty() )
            return result;

        PRICE_CACHE cache;
        auto        sources = makeSources( aOptions.sources );
        auto        deadline = std::chrono::steady_clock::now()
                               + std::chrono::seconds( std::max( aOptions.totalBudgetSecs, 1 ) );

        HTTP_GET_HANDLER http = DefaultHttpHandler();
        bool             throttleNeeded = false;

        auto expired = [&]() -> bool
        {
            if( aCancelled && aCancelled() )
                return true;

            return std::chrono::steady_clock::now() >= deadline;
        };

        for( const auto& [mpn, qty] : wanted )
        {
            // Cache first — this is also what keeps dialog and CLI answers identical.
            // A forced refresh skips the read but still writes, so the fresh numbers
            // become the shared answer for every later consumer.
            std::optional<CACHED_PRICE> cached;

            if( !aOptions.forceRefresh )
                cached = cache.Get( mpn, aOptions.cacheTtlHours );

            if( cached )
            {
                if( !cached->notFound )
                {
                    double price = cached->priceInr;

                    if( !cached->qtyBreaksInr.empty() )
                    {
                        std::vector<PRICE_BREAK> breaks;

                        for( const auto& [bQty, bPrice] : cached->qtyBreaksInr )
                            breaks.push_back( { bQty, bPrice } );

                        price = PriceAtQty( breaks, qty );
                    }

                    if( price > 0.0 )
                        result[mpn] = formatInr( price );
                }

                continue;
            }

            if( expired() )
                continue;   // out of budget: remaining parts stay cache-or-empty

            // Compare every offer from every enabled source and keep the lowest INR unit
            // price at this qty.
            CACHED_PRICE best;
            best.notFound = true;

            for( const auto& source : sources )
            {
                if( expired() )
                    break;

                if( throttleNeeded )
                {
                    std::this_thread::sleep_for(
                            std::chrono::milliseconds( INTER_REQUEST_DELAY_MS ) );
                }

                throttleNeeded = true;

                std::vector<PRICE_QUOTE> sourceQuotes;

                for( int attempt = 0; attempt < MAX_ATTEMPTS_PER_REQUEST; ++attempt )
                {
                    sourceQuotes = source->FetchQuotes( mpn, qty );

                    if( !sourceQuotes.empty() || expired() )
                        break;
                }

                for( const PRICE_QUOTE& quote : sourceQuotes )
                {
                    if( !quote.found || quote.price <= 0.0 )
                        continue;

                    double toInr = 1.0;

                    if( quote.currency.CmpNoCase( wxS( "INR" ) ) == 0 )
                        toInr = 1.0;
                    else if( quote.currency.CmpNoCase( wxS( "USD" ) ) == 0 )
                        toInr = GetUsdToInr( cache, http );
                    else
                        continue;   // a currency we can't convert faithfully: not comparable

                    double priceInr = quote.price * toInr;

                    if( best.notFound || priceInr < best.priceInr )
                    {
                        best.notFound = false;
                        best.priceInr = priceInr;
                        best.source = quote.sourceName;
                        best.url = quote.productUrl;
                        best.qtyBreaksInr.clear();

                        for( const PRICE_BREAK& b : quote.breaks )
                            best.qtyBreaksInr.emplace_back( b.qty, b.price * toInr );
                    }
                }
            }

            cache.Put( mpn, best );

            if( !best.notFound )
                result[mpn] = formatInr( best.priceInr );
        }

        cache.Flush();
    }
    catch( ... )
    {
        // Pricing is best-effort everywhere: whatever resolved before the failure still counts.
    }

    return result;
}

} // namespace BOM_PRICING
