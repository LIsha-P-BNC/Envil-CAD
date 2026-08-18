/*
 * This program source code file is part of KiCad, a free EDA CAD application.
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

#include <wx/string.h>

class FIELDS_EDITOR_GRID_DATA_MODEL;
struct BOM_FMT_PRESET;


/**
 * Writes the contents of a FIELDS_EDITOR_GRID_DATA_MODEL as a native .xlsx workbook
 * laid out like an Altium Designer BOM report:
 *
 *  - Sheet "Manufacturing": title block, styled header row, one row per (grouped) BOM
 *    line, S.No and Subtotal formulas, totals row, sign-off row, legend block,
 *    AutoFilter dropdowns on every column and "NP" conditional highlighting on the
 *    value column.
 *  - Sheet "Project Information": project/report metadata as key-value rows.
 */
class BOM_XLSX_WRITER
{
public:
    struct PROJECT_INFO
    {
        wxString projectFullPath;
        wxString projectFilename;
        wxString variantName;
        wxString sourceFilename;
        wxString sourceFullPath;
        wxString title;
        wxString releaseTag;     ///< Shown in the "Release:" box; defaults to "NR" if empty
    };

    /**
     * Write the BOM to aOutPath.  Only shown columns are exported; CHILD_ITEM rows
     * (expanded group members) are skipped, matching the CSV export behaviour.
     *
     * @return true on success; on failure aErrMsg (if non-null) receives a description.
     */
    static bool Write( const wxString& aOutPath, FIELDS_EDITOR_GRID_DATA_MODEL* aModel,
                       const BOM_FMT_PRESET& aFmt, const PROJECT_INFO& aInfo,
                       wxString* aErrMsg = nullptr );
};
