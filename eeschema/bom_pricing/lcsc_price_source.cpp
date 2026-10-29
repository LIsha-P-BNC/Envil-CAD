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

#include "lcsc_price_source.h"
#include "bom_pricing_defs.h"

#include <json_common.h>

#include <wx/string.h>

namespace BOM_PRICING
{

namespace
{

double numberField( const nlohmann::json& aObj, const char* aField )
{
    auto it = aObj.find( aField );

    if( it == aObj.end() )
        return 0.0;

    if( it->is_number() )
        return it->get<double>();

    if( it->is_string() )
    {
        double value = 0.0;
        wxString( it->get<std::string>() ).ToCDouble( &value );
        return value;
    }

    return 0.0;
}


std::string stringField( const nlohmann::json& aObj, const char* aField )
{
    auto it = aObj.find( aField );

    if( it != aObj.end() && it->is_string() )
        return it->get<std::string>();

    return {};
}


/// The ladder: componentPrices = [{startNumber, endNumber, productPrice}], prices in USD.
std::vector<PRICE_BREAK> readPriceLadder( const nlohmann::json& aComponent )
{
    std::vector<PRICE_BREAK> breaks;

    auto it = aComponent.find( "componentPrices" );

    if( it == aComponent.end() || !it->is_array() )
        return breaks;

    for( const nlohmann::json& entry : *it )
    {
        if( !entry.is_object() )
            continue;

        PRICE_BREAK pb;
        pb.qty = (int) numberField( entry, "startNumber" );
        pb.price = numberField( entry, "productPrice" );

        if( pb.qty >= 1 && pb.price > 0.0 )
            breaks.push_back( pb );
    }

    return breaks;
}


/// JSON-escape the few characters an MPN could carry into the search body.
std::string escapeForJson( const wxString& aText )
{
    std::string out;

    for( char c : std::string( aText.utf8_str() ) )
    {
        if( c == '"' || c == '\\' )
            out += '\\';

        if( (unsigned char) c >= 0x20 )
            out += c;
    }

    return out;
}

} // namespace


std::vector<PRICE_QUOTE> LCSC_PRICE_SOURCE::FetchQuotes( const wxString& aMpn, int aQty )
{
    std::vector<PRICE_QUOTE> quotes;

    try
    {
        HTTP_GET_REQUEST req;
        req.url = JLC_SEARCH_URL;
        req.postBody = wxString::Format( wxString::FromUTF8( JLC_SEARCH_BODY_FMT ),
                                         wxString::FromUTF8( escapeForJson( aMpn ) ) )
                               .ToStdString();
        req.headers = { { "Accept", "application/json" } };
        req.timeoutSecs = PER_REQUEST_TIMEOUT_SECS;

        HTTP_GET_RESPONSE rsp;

        if( !m_http( req, rsp ) || rsp.status != 200 )
            return quotes;

        nlohmann::json js = nlohmann::json::parse( rsp.body, nullptr, /* throw */ false );

        if( js.is_discarded() || !js.is_object() )
            return quotes;

        const nlohmann::json* list = nullptr;

        if( auto data = js.find( "data" ); data != js.end() && data->is_object() )
        {
            if( auto page = data->find( "componentPageInfo" ); page != data->end() && page->is_object() )
            {
                if( auto l = page->find( "list" ); l != page->end() && l->is_array() )
                    list = &( *l );
            }
        }

        if( !list )
            return quotes;

        // Exact model match wins; a model merely containing the MPN is only a fallback.
        const nlohmann::json* match = nullptr;

        for( const nlohmann::json& component : *list )
        {
            if( !component.is_object() )
                continue;

            wxString model = wxString::FromUTF8( stringField( component, "componentModelEn" ) );

            if( model.CmpNoCase( aMpn ) == 0 )
            {
                match = &component;
                break;
            }

            if( !match && model.Upper().Contains( aMpn.Upper() ) )
                match = &component;
        }

        if( !match )
            return quotes;

        std::vector<PRICE_BREAK> breaks = readPriceLadder( *match );

        if( breaks.empty() )
            return quotes;

        PRICE_QUOTE quote;
        quote.found = true;
        quote.sourceName = Name();
        quote.currency = wxS( "USD" );
        quote.breaks = std::move( breaks );
        quote.price = PriceAtQty( quote.breaks, aQty );
        quote.productUrl = wxString::FromUTF8( stringField( *match, "lcscGoodsUrl" ) );

        quotes.push_back( std::move( quote ) );
    }
    catch( ... )
    {
        quotes.clear();
    }

    return quotes;
}

} // namespace BOM_PRICING
