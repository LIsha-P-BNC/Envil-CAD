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

#include <kicad_curl/kicad_curl_easy.h>

#include "price_source.h"
#include "bom_pricing_defs.h"

#include <algorithm>

namespace BOM_PRICING
{

HTTP_GET_HANDLER DefaultHttpHandler()
{
    return []( const HTTP_GET_REQUEST& aRequest, HTTP_GET_RESPONSE& aResponse ) -> bool
    {
        try
        {
            KICAD_CURL_EASY curl;

            curl.SetURL( aRequest.url );
            curl.SetUserAgent( BROWSER_USER_AGENT );
            curl.SetFollowRedirects( aRequest.followRedirects );
            curl.SetConnectTimeout( aRequest.timeoutSecs );
            curl.SetTimeout( aRequest.timeoutSecs );

            for( const auto& [name, value] : aRequest.headers )
                curl.SetHeader( name, value );

            if( !aRequest.postBody.empty() )
            {
                curl.SetHeader( "Content-Type", aRequest.contentType );
                curl.SetPostFields( aRequest.postBody );
            }

            if( curl.Perform() != 0 )
                return false;

            aResponse.status = curl.GetResponseStatusCode();
            aResponse.body = curl.GetBuffer();
            return true;
        }
        catch( ... )
        {
            return false;
        }
    };
}


std::string UrlEscape( const wxString& aText )
{
    try
    {
        KICAD_CURL_EASY curl;
        return curl.Escape( std::string( aText.utf8_str() ) );
    }
    catch( ... )
    {
        return std::string( aText.utf8_str() );
    }
}


double PriceAtQty( const std::vector<PRICE_BREAK>& aBreaks, int aQty )
{
    if( aBreaks.empty() )
        return 0.0;

    const PRICE_BREAK* best = nullptr;
    const PRICE_BREAK* lowest = &aBreaks.front();

    for( const PRICE_BREAK& b : aBreaks )
    {
        if( b.qty < lowest->qty )
            lowest = &b;

        if( b.qty <= aQty && ( !best || b.qty > best->qty ) )
            best = &b;
    }

    return best ? best->price : lowest->price;
}

} // namespace BOM_PRICING
