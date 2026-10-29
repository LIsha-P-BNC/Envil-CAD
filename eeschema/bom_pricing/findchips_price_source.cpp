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

#include "findchips_price_source.h"
#include "bom_pricing_defs.h"

#include <json_common.h>

#include <wx/string.h>

namespace BOM_PRICING
{

namespace
{

/// Decode the HTML entities Findchips uses inside attribute values.
std::string decodeEntities( const std::string& aText )
{
    std::string out;
    out.reserve( aText.size() );

    for( size_t i = 0; i < aText.size(); )
    {
        if( aText.compare( i, 5, "&#34;" ) == 0 )
        {
            out += '"';
            i += 5;
        }
        else if( aText.compare( i, 6, "&quot;" ) == 0 )
        {
            out += '"';
            i += 6;
        }
        else if( aText.compare( i, 5, "&amp;" ) == 0 )
        {
            out += '&';
            i += 5;
        }
        else
        {
            out += aText[i];
            i += 1;
        }
    }

    return out;
}


/// The raw value of one data-* attribute inside a tag, or empty.
std::string attrValue( const std::string& aTag, const std::string& aAttr )
{
    std::string needle = aAttr + "=\"";
    size_t      pos = aTag.find( needle );

    if( pos == std::string::npos )
        return {};

    pos += needle.size();
    size_t end = aTag.find( '"', pos );

    if( end == std::string::npos )
        return {};

    return aTag.substr( pos, end - pos );
}

} // namespace


std::vector<PRICE_QUOTE> FINDCHIPS_PRICE_SOURCE::FetchQuotes( const wxString& aMpn, int aQty )
{
    std::vector<PRICE_QUOTE> quotes;

    try
    {
        HTTP_GET_REQUEST req;
        req.url = wxString::Format( wxString::FromUTF8( FINDCHIPS_SEARCH_URL_FMT ),
                                    wxString::FromUTF8( UrlEscape( aMpn ) ) ).ToStdString();
        req.headers = { { "Accept", "text/html,application/xhtml+xml" } };
        req.timeoutSecs = PER_REQUEST_TIMEOUT_SECS;

        HTTP_GET_RESPONSE rsp;

        if( !m_http( req, rsp ) || rsp.status != 200 )
            return quotes;

        // One offer row per data-price attribute; the same tag carries the distributor name
        // and the exact manufacturer part number the offer is for.
        const std::string marker = "data-price=\"";
        size_t            pos = 0;

        while( ( pos = rsp.body.find( marker, pos ) ) != std::string::npos )
        {
            size_t tagStart = rsp.body.rfind( '<', pos );
            size_t tagEnd = rsp.body.find( '>', pos );

            pos += marker.size();

            if( tagStart == std::string::npos || tagEnd == std::string::npos )
                continue;

            std::string tag = rsp.body.substr( tagStart, tagEnd - tagStart );

            // Offers for other members of the part family (e.g. the -TR reel variant of a cut
            // tape part) are not this part's price.
            wxString offerMpn =
                    wxString::FromUTF8( decodeEntities( attrValue( tag, "data-mfrpartnumber" ) ) );
            offerMpn.Trim( true ).Trim( false );

            if( offerMpn.CmpNoCase( aMpn ) != 0 )
                continue;

            nlohmann::json ladder = nlohmann::json::parse(
                    decodeEntities( attrValue( tag, "data-price" ) ), nullptr, /* throw */ false );

            if( ladder.is_discarded() || !ladder.is_array() || ladder.empty() )
                continue;

            PRICE_QUOTE quote;

            // Ladder entries are [qty, "CUR", "price"]; a row quotes one currency throughout.
            for( const nlohmann::json& entry : ladder )
            {
                if( !entry.is_array() || entry.size() < 3 )
                    continue;

                PRICE_BREAK pb;

                if( entry[0].is_number() )
                    pb.qty = entry[0].get<int>();

                double price = 0.0;

                if( entry[2].is_number() )
                    price = entry[2].get<double>();
                else if( entry[2].is_string() )
                    wxString( entry[2].get<std::string>() ).ToCDouble( &price );

                pb.price = price;

                if( quote.currency.IsEmpty() && entry[1].is_string() )
                    quote.currency = wxString::FromUTF8( entry[1].get<std::string>() ).Upper();

                if( pb.qty >= 1 && pb.price > 0.0 )
                    quote.breaks.push_back( pb );
            }

            if( quote.breaks.empty() || quote.currency.IsEmpty() )
                continue;

            wxString distributor =
                    wxString::FromUTF8( decodeEntities( attrValue( tag, "data-distributor_name" ) ) );

            quote.found = true;
            quote.sourceName = distributor.IsEmpty() ? Name()
                                                     : distributor + wxS( " (Findchips)" );
            quote.price = PriceAtQty( quote.breaks, aQty );
            quote.productUrl = wxString::FromUTF8( decodeEntities( attrValue( tag, "data-url" ) ) );

            quotes.push_back( std::move( quote ) );
        }
    }
    catch( ... )
    {
        quotes.clear();
    }

    return quotes;
}

} // namespace BOM_PRICING
