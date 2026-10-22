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

#ifndef PANEL_SCH_PASSIVE_PARTS_H
#define PANEL_SCH_PASSIVE_PARTS_H

#include <vector>

#include <widgets/resettable_panel.h>

#include <passive_parts/passive_part_table.h>

class wxButton;
class wxCheckBox;
class wxListBox;
class wxStaticText;
class wxTextCtrl;
class WX_GRID;

/**
 * Preferences > Schematic Editor > Passive Parts.
 *
 * Lets a customer import, inspect, export and remove their company's passive parts
 * lists (CSV), which drive BOM auto-fill for passives.  Lists are stored at
 * application level (see PASSIVE_PARTS::PASSIVE_TABLE_STORE) and survive upgrades.
 */
class PANEL_SCH_PASSIVE_PARTS : public RESETTABLE_PANEL
{
public:
    PANEL_SCH_PASSIVE_PARTS( wxWindow* aParent );
    ~PANEL_SCH_PASSIVE_PARTS() override;

    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;

    void ResetPanel() override;

private:
    void reloadTables( const wxString& aSelectId = wxEmptyString );
    void updateSelection();
    void updatePreview();

    PASSIVE_PARTS::PASSIVE_TABLE* selectedTable();

    void onImport( wxCommandEvent& aEvent );
    void onExport( wxCommandEvent& aEvent );
    void onRemove( wxCommandEvent& aEvent );
    void onSelectTable( wxCommandEvent& aEvent );
    void onToggleEnabled( wxCommandEvent& aEvent );

    std::vector<PASSIVE_PARTS::PASSIVE_TABLE> m_tables;
    std::vector<bool>                         m_enabledDirty;

    wxCheckBox*   m_enableFill;
    wxTextCtrl*   m_defaultTolerance;
    wxListBox*    m_tableList;
    wxCheckBox*   m_tableEnabled;
    wxStaticText* m_tableInfo;
    WX_GRID*      m_preview;
    wxButton*     m_importButton;
    wxButton*     m_exportButton;
    wxButton*     m_removeButton;
};

#endif // PANEL_SCH_PASSIVE_PARTS_H
