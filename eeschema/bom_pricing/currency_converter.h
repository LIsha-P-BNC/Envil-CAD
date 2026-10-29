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

#ifndef CURRENCY_CONVERTER_H
#define CURRENCY_CONVERTER_H

#include "price_source.h"

namespace BOM_PRICING
{

class PRICE_CACHE;

/**
 * The day's USD->INR rate: cached value first, then the keyless FX endpoints, then the
 * compiled-in fallback constant — a BOM is never left unpriced because FX was unreachable.
 * At most one network fetch per 24 h (the fetched rate is stored back into aCache).
 */
double GetUsdToInr( PRICE_CACHE& aCache, const HTTP_GET_HANDLER& aHttp );

} // namespace BOM_PRICING

#endif // CURRENCY_CONVERTER_H
