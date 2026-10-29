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

#ifndef PRICE_CACHE_H
#define PRICE_CACHE_H

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <wx/string.h>

namespace BOM_PRICING
{

struct CACHED_PRICE
{
    double                               priceInr = 0.0;
    wxString                             source;
    wxString                             url;
    std::vector<std::pair<int, double>>  qtyBreaksInr;   // (qty, unit price in INR)
    bool                                 notFound = false;
    int64_t                              fetchedAt = 0;  // epoch seconds
};

/**
 * The on-disk price cache shared by the BOM dialog and the CLI exporter —
 * %APPDATA%\anvil\1.0\bom_price_cache.json.
 *
 * This file is the desktop/AI parity mechanism: within the TTL both consumers answer from the
 * same entries, so their BOMs carry byte-identical prices.  Keys are normalized MPNs
 * (Trim + Upper).  Negative entries (part not found anywhere) live shorter than positive ones
 * so unknown parts recover quickly without hammering the sources.
 *
 * Concurrent writers (dialog and CLI can race): Flush() re-reads the file, overlays only this
 * session's new entries, and renames a temp file into place — last writer wins over an
 * identical merged base, so nothing is lost.
 */
class PRICE_CACHE
{
public:
    PRICE_CACHE();

    /// nullopt when absent or expired (positive TTL from settings; negative TTL is fixed).
    std::optional<CACHED_PRICE> Get( const wxString& aNormalizedMpn, int aTtlHours ) const;

    void Put( const wxString& aNormalizedMpn, const CACHED_PRICE& aEntry );

    /// The cached daily USD->INR rate; nullopt when absent or stale.
    std::optional<double> GetFreshUsdInr() const;
    void                  PutUsdInr( double aRate );

    /// Merge this session's new entries into the file on disk (read-merge-rename).
    void Flush();

    static wxString CacheFilePath();

private:
    void load();

    std::map<wxString, CACHED_PRICE> m_parts;
    std::map<wxString, CACHED_PRICE> m_dirty;      // written this session, pending Flush()

    double  m_usdInr = 0.0;
    int64_t m_usdInrFetchedAt = 0;
    bool    m_usdInrDirty = false;
};

} // namespace BOM_PRICING

#endif // PRICE_CACHE_H
