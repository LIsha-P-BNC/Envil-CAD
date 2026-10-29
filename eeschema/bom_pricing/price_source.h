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

#ifndef PRICE_SOURCE_H
#define PRICE_SOURCE_H

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <wx/string.h>

namespace BOM_PRICING
{

struct PRICE_BREAK
{
    int    qty = 1;
    double price = 0.0;     // in the quote's currency
};

struct PRICE_QUOTE
{
    bool                     found = false;
    double                   price = 0.0;       // unit price at the requested quantity
    wxString                 currency;          // ISO code, e.g. "USD", "INR"
    std::vector<PRICE_BREAK> breaks;
    wxString                 sourceName;
    wxString                 productUrl;
};

struct HTTP_GET_REQUEST
{
    std::string                                      url;
    std::vector<std::pair<std::string, std::string>> headers;
    long                                             timeoutSecs = 5;
    bool                                             followRedirects = true;
    std::string                                      postBody;      // non-empty makes it a POST
    std::string                                      contentType = "application/json";
};

struct HTTP_GET_RESPONSE
{
    int         status = 0;
    std::string body;
};

/// Returns false only on transport failure; HTTP error statuses come back in the response.
using HTTP_GET_HANDLER = std::function<bool( const HTTP_GET_REQUEST&, HTTP_GET_RESPONSE& )>;

/// The real transport (KICAD_CURL_EASY).  Sources default to it; tests inject a fake.
HTTP_GET_HANDLER DefaultHttpHandler();

/// URL-escape one query component (curl's escaper, no live handle needed by callers).
std::string UrlEscape( const wxString& aText );

/// Unit price for aQty from a price-break ladder: highest break <= aQty, else the lowest break.
double PriceAtQty( const std::vector<PRICE_BREAK>& aBreaks, int aQty );

/**
 * One lookup site.  A site can quote several offers (an aggregator returns one per
 * distributor); the service converts them all to INR and keeps the cheapest.
 * Implementations must never throw out of FetchQuotes() and must return an empty vector on
 * any failure (blocked, parse error, no match) — a missing source is never an error, just a
 * narrower comparison.
 */
class PRICE_SOURCE
{
public:
    virtual ~PRICE_SOURCE() = default;

    virtual wxString Name() const = 0;
    virtual std::vector<PRICE_QUOTE> FetchQuotes( const wxString& aMpn, int aQty ) = 0;

    void SetHttpHandler( HTTP_GET_HANDLER aHandler ) { m_http = std::move( aHandler ); }

protected:
    HTTP_GET_HANDLER m_http = DefaultHttpHandler();
};

} // namespace BOM_PRICING

#endif // PRICE_SOURCE_H
