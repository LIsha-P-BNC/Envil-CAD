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

#include "currency_converter.h"
#include "bom_pricing_defs.h"
#include "price_cache.h"

#include <json_common.h>

namespace BOM_PRICING
{

namespace
{

/// The INR rate from one FX endpoint; both known payloads carry it as rates.INR.
double fetchRate( const HTTP_GET_HANDLER& aHttp, const char* aUrl )
{
    try
    {
        HTTP_GET_REQUEST req;
        req.url = aUrl;
        req.headers = { { "Accept", "application/json" } };
        req.timeoutSecs = PER_REQUEST_TIMEOUT_SECS;

        HTTP_GET_RESPONSE rsp;

        if( !aHttp( req, rsp ) || rsp.status != 200 )
            return 0.0;

        nlohmann::json js = nlohmann::json::parse( rsp.body, nullptr, /* throw */ false );

        if( js.is_discarded() || !js.is_object() )
            return 0.0;

        auto ratesIt = js.find( "rates" );

        if( ratesIt == js.end() || !ratesIt->is_object() )
            return 0.0;

        auto inrIt = ratesIt->find( "INR" );

        if( inrIt != ratesIt->end() && inrIt->is_number() )
            return inrIt->get<double>();
    }
    catch( ... )
    {
    }

    return 0.0;
}

} // namespace


double GetUsdToInr( PRICE_CACHE& aCache, const HTTP_GET_HANDLER& aHttp )
{
    if( std::optional<double> cached = aCache.GetFreshUsdInr() )
        return *cached;

    double rate = fetchRate( aHttp, FX_PRIMARY_URL );

    if( rate <= 0.0 )
        rate = fetchRate( aHttp, FX_FALLBACK_URL );

    if( rate > 0.0 )
    {
        aCache.PutUsdInr( rate );
        return rate;
    }

    return FALLBACK_USD_INR;
}

} // namespace BOM_PRICING
