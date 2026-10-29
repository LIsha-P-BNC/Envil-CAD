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

#ifndef ELEMENT14_PRICE_SOURCE_H
#define ELEMENT14_PRICE_SOURCE_H

#include "price_source.h"

namespace BOM_PRICING
{

/**
 * element14 India lookup (INR prices).
 *
 * With an API key set, uses the official Product Search API (api.element14.com) — reliable
 * JSON with full quantity-break ladders, no bot wall.  Without a key, falls back to scraping
 * the server-rendered product pages' JSON-LD markup, where a bot challenge (403/JS wall)
 * simply yields found=false.
 */
class ELEMENT14_PRICE_SOURCE : public PRICE_SOURCE
{
public:
    wxString Name() const override { return wxS( "element14 IN" ); }
    std::vector<PRICE_QUOTE> FetchQuotes( const wxString& aMpn, int aQty ) override;

    void SetApiKey( const wxString& aKey ) { m_apiKey = aKey; }

private:
    std::vector<PRICE_QUOTE> fetchViaApi( const wxString& aMpn, int aQty );
    std::vector<PRICE_QUOTE> fetchViaScrape( const wxString& aMpn, int aQty );

    wxString m_apiKey;
};

} // namespace BOM_PRICING

#endif // ELEMENT14_PRICE_SOURCE_H
