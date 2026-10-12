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

#ifndef BOM_PRICE_SERVICE_H
#define BOM_PRICE_SERVICE_H

#include <functional>
#include <map>
#include <vector>

#include <wx/string.h>

namespace BOM_PRICING
{

/**
 * Resolve BOM line items to their lowest INR unit price across the enabled keyless
 * distributor sources, cache-first.
 *
 * Synchronous and self-contained: the BOM dialog calls it from a thread-pool task, the CLI
 * exporter calls it inline.  Both go through the same on-disk cache, which is what keeps the
 * desktop BOM and the AI-generated BOM showing identical numbers.  Never throws; parts that
 * can't be priced are simply absent from the result.
 */
class BOM_PRICE_SERVICE
{
public:
    struct PART_REQUEST
    {
        wxString mpn;
        int      qty = 1;
    };

    struct OPTIONS
    {
        int      cacheTtlHours = 24;
        int      fetchTimeoutSecs = 5;
        int      totalBudgetSecs = 60;
        wxString sources = wxS( "lcsc,findchips" );   // comma-separated source ids

        /// Fetch live even when a fresh cached entry exists (the dialog's Refresh Prices
        /// button).  Results still land in the cache, so the CLI/AI path sees them too.
        bool     forceRefresh = false;
    };

    /// The cache/overlay key form: trimmed, uppercased MPN.
    static wxString NormalizeMpn( const wxString& aMpn );

    /**
     * @return normalized MPN -> unit price in INR as a plain locale-independent "123.45"
     *         string (no currency symbol, so exports stay numeric and sortable).
     */
    std::map<wxString, wxString> GetPrices( const std::vector<PART_REQUEST>& aParts,
                                            const OPTIONS& aOptions,
                                            const std::function<bool()>& aCancelled = {} );
};

} // namespace BOM_PRICING

#endif // BOM_PRICE_SERVICE_H
