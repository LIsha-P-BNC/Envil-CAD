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

#ifndef PASSIVE_TABLE_STORE_H
#define PASSIVE_TABLE_STORE_H

#include <mutex>

#include "passive_part_table.h"

namespace PASSIVE_PARTS
{

/**
 * Application-level storage for imported passive parts lists.
 *
 * Each list lives in its own JSON file under <user settings>/passives/ (so it survives
 * application upgrades: the user settings dir is version-pinned), written atomically via
 * a pid-suffixed temp file + rename, read tolerantly (a corrupt file degrades to "no
 * table", never an error).  This mirrors the fork's bom_price_cache.json discipline.
 *
 * Used from both the BOM dialog (main thread) and anvil-cli jobs; a mutex guards the
 * lazy load.  All lookups are read-only after load.
 */
class PASSIVE_TABLE_STORE
{
public:
    static PASSIVE_TABLE_STORE& Get();

    /// <user settings>/passives, created on demand.
    static wxString StoreDirPath();

    /// All stored tables, sorted by order then name.  Lazy-loads on first use.
    std::vector<PASSIVE_TABLE> Tables();

    /// Drop the cache; next access re-scans the directory.
    void Reload();

    /// true if at least one enabled, non-empty table exists.
    bool HasEnabledTables();

    /**
     * Write aTable to its payload file (atomic).  Assigns a fresh id when empty.
     * Refreshes the in-memory cache.  Returns false on I/O failure.
     */
    bool Save( PASSIVE_TABLE& aTable );

    /// Delete the payload file for aId and refresh the cache.
    bool Remove( const wxString& aId );

    /**
     * Look up the record for a symbol across all enabled tables (order, then name).
     *
     * Tolerance resolution: if aSymbolTolerance is non-empty, only an exact match wins.
     * Otherwise a single candidate wins outright; with several, the first whose
     * tolerance equals the owning table's default (or aFallbackTolerance) wins; if none
     * does, nothing is returned — a wrong MPN is worse than an empty cell.
     *
     * @return a copy-safe pointer into the internal cache; invalidated by Reload/Save/
     *         Remove.  Null if no unambiguous match.
     */
    const PASSIVE_RECORD* Find( PASSIVE_TYPE aType, const wxString& aPackage,
                                int64_t aValueScaled, const wxString& aSymbolTolerance,
                                const wxString& aFallbackTolerance );

    /// Union of shownBomFields over enabled tables, deduplicated by normalized name.
    std::vector<wxString> ShownBomFields();

private:
    PASSIVE_TABLE_STORE() = default;

    void ensureLoaded();
    void loadLocked();

    std::mutex                 m_mutex;
    bool                       m_loaded = false;
    std::vector<PASSIVE_TABLE> m_tables;
};

} // namespace PASSIVE_PARTS

#endif // PASSIVE_TABLE_STORE_H
