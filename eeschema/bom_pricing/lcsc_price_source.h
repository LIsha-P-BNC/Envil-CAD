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

#ifndef LCSC_PRICE_SOURCE_H
#define LCSC_PRICE_SOURCE_H

#include "price_source.h"

namespace BOM_PRICING
{

/// Keyless LCSC catalog pricing (USD) via JLCPCB's parts API.
class LCSC_PRICE_SOURCE : public PRICE_SOURCE
{
public:
    wxString Name() const override { return wxS( "LCSC" ); }
    std::vector<PRICE_QUOTE> FetchQuotes( const wxString& aMpn, int aQty ) override;
};

} // namespace BOM_PRICING

#endif // LCSC_PRICE_SOURCE_H
