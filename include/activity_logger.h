/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <mutex>
#include <vector>

#include <wx/string.h>

#include <kicommon.h>

/**
 * Append-only activity log of design edits (one JSON line per committed change).
 *
 * Every model-changing operation in the schematic and board editors funnels through
 * COMMIT::Push; the commit hooks call LogCommit() so the log records what changed,
 * which items were affected, and whether the edit was interactive ("manual") or made
 * through the IPC API ("api", i.e. AI / external tools).
 *
 * Each project gets its own log: <project dir>/<project name>-activity.log, so the
 * history travels with the project.  Edits made outside a project (standalone editing,
 * unsaved work) fall back to <user settings dir>/activity.log, as do edits whose
 * project directory is not writable.
 *
 * Logging is passive: any failure to write is silently ignored and never affects the
 * edit itself.
 */
class KICOMMON_API ACTIVITY_LOGGER
{
public:
    static ACTIVITY_LOGGER& Get();

    /**
     * Append one commit entry to the project's log.
     *
     * @param aProjectDir absolute path of the project directory; empty if no project is
     *                    loaded (entry then goes to the global fallback log).
     * @param aProjectName project file name without extension or path; may be empty.
     * @param aEditor which editor produced the edit ("pcb", "footprint", "schematic", "symbol").
     * @param aMessage the commit message, e.g. "Move" or "Draw Wire".
     * @param aFromAPI true if the commit originated from the IPC API rather than user input.
     * @param aItems one description per affected item ("modify: Footprint R5 (uuid)").
     */
    void LogCommit( const wxString& aProjectDir, const wxString& aProjectName,
                    const wxString& aEditor, const wxString& aMessage, bool aFromAPI,
                    const std::vector<wxString>& aItems );

private:
    ACTIVITY_LOGGER() = default;

    /// Append aLine to aPath; returns false on any failure.
    static bool appendLine( const wxString& aPath, const wxString& aLine );

    static wxString jsonEscape( const wxString& aStr );

private:
    std::mutex m_mutex;
};
