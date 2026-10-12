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

#ifndef FINDCHIPS_PRICE_SOURCE_H
#define FINDCHIPS_PRICE_SOURCE_H

#include "price_source.h"

namespace BOM_PRICING
{

/**
 * Keyless multi-distributor comparison via Findchips' server-rendered search page: one GET
 * returns offer rows from DigiKey, Mouser, element14 (INR for Asia-Pacific), Arrow, TME, ...
 * each carrying machine-readable data-price / data-distributor_name / data-mfrpartnumber
 * attributes.  One quote is returned per matching offer row; the service keeps the cheapest.
 */
class FINDCHIPS_PRICE_SOURCE : public PRICE_SOURCE
{
public:
    wxString Name() const override { return wxS( "Findchips" ); }
    std::vector<PRICE_QUOTE> FetchQuotes( const wxString& aMpn, int aQty ) override;
};

} // namespace BOM_PRICING

#endif // FINDCHIPS_PRICE_SOURCE_H
