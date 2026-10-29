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

#include "panel_sch_passive_parts.h"

#include <algorithm>
#include <set>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/datetime.h>
#include <wx/filename.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wfstream.h>

#include <confirm.h>
#include <dialog_shim.h>
#include <eeschema_settings.h>
#include <grid_tricks.h>
#include <io/csv.h>
#include <kiplatform/ui.h>
#include <richio.h>
#include <settings/settings_manager.h>
#include <sim/sim_value.h>
#include <widgets/ui_common.h>
#include <widgets/wx_grid.h>
#include <wildcards_and_files_ext.h>

#include <fields_data_model.h>
#include <passive_parts/passive_table_store.h>

using namespace PASSIVE_PARTS;


// --------------------------------------------------------------------------------------
// Import dialog: column mapping + list metadata
// --------------------------------------------------------------------------------------

namespace
{

enum MAP_COL
{
    COL_CSV_HEADER = 0,
    COL_SAMPLE,
    COL_ROLE,
    COL_BOM_FIELD,
    COL_SHOW,
    COL_COUNT
};

// ROLE_ prefix: bare IGNORE (and friends) collide with Windows API macros
enum class COLUMN_ROLE
{
    ROLE_IGNORE = 0,
    ROLE_VALUE,
    ROLE_PACKAGE,
    ROLE_TOLERANCE,
    ROLE_TYPE,
    ROLE_BOM_FIELD
};


wxString roleName( COLUMN_ROLE aRole )
{
    switch( aRole )
    {
    case COLUMN_ROLE::ROLE_VALUE:     return _( "Value" );
    case COLUMN_ROLE::ROLE_PACKAGE:   return _( "Package" );
    case COLUMN_ROLE::ROLE_TOLERANCE: return _( "Tolerance" );
    case COLUMN_ROLE::ROLE_TYPE:      return _( "Type" );
    case COLUMN_ROLE::ROLE_BOM_FIELD: return _( "BOM Field" );
    default:                     return _( "Ignore" );
    }
}


COLUMN_ROLE roleFromName( const wxString& aName )
{
    for( COLUMN_ROLE role : { COLUMN_ROLE::ROLE_VALUE, COLUMN_ROLE::ROLE_PACKAGE, COLUMN_ROLE::ROLE_TOLERANCE,
                              COLUMN_ROLE::ROLE_TYPE, COLUMN_ROLE::ROLE_BOM_FIELD } )
    {
        if( aName == roleName( role ) )
            return role;
    }

    return COLUMN_ROLE::ROLE_IGNORE;
}


wxString normalizedHeader( const wxString& aHeader )
{
    wxString out;

    for( wxUniChar c : aHeader.Lower() )
    {
        if( c != ' ' && c != '.' && c != '_' && c != '-' && c != '#' )
            out += c;
    }

    return out;
}


/// True for a column of consecutive ascending integers (a serial-number column) -- never
/// a value column no matter how well it parses.
bool looksLikeSerialColumn( const std::vector<std::vector<wxString>>& aData, size_t aCol,
                            size_t aFirstDataRow )
{
    long previous = 0;
    bool first = true;

    for( size_t i = aFirstDataRow; i < aData.size(); ++i )
    {
        if( aCol >= aData[i].size() )
            return false;

        wxString cell = aData[i][aCol];
        cell.Trim( true ).Trim( false );

        if( cell.IsEmpty() )
            continue;

        long numeric = 0;

        if( !cell.ToLong( &numeric ) )
            return false;

        if( !first && numeric != previous + 1 )
            return false;

        previous = numeric;
        first = false;
    }

    return !first;
}


class DIALOG_PASSIVE_LIST_IMPORT : public DIALOG_SHIM
{
public:
    DIALOG_PASSIVE_LIST_IMPORT( wxWindow* aParent, const wxString& aSourcePath,
                                const std::vector<std::vector<wxString>>& aData ) :
            DIALOG_SHIM( aParent, wxID_ANY, _( "Import Passive Parts List" ), wxDefaultPosition,
                         wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
            m_sourcePath( aSourcePath ),
            m_data( aData )
    {
        wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

        wxFlexGridSizer* metaSizer = new wxFlexGridSizer( 2, 4, FromDIP( 6 ), FromDIP( 8 ) );
        metaSizer->AddGrowableCol( 1 );
        metaSizer->AddGrowableCol( 3 );

        metaSizer->Add( new wxStaticText( this, wxID_ANY, _( "List name:" ) ), 0,
                        wxALIGN_CENTER_VERTICAL );
        m_name = new wxTextCtrl( this, wxID_ANY );
        metaSizer->Add( m_name, 1, wxEXPAND );

        metaSizer->Add( new wxStaticText( this, wxID_ANY, _( "Component type:" ) ), 0,
                        wxALIGN_CENTER_VERTICAL );
        m_type = new wxChoice( this, wxID_ANY );
        m_type->Append( _( "Resistor" ) );
        m_type->Append( _( "Capacitor" ) );
        m_type->Append( _( "Inductor" ) );
        m_type->SetSelection( 0 );
        metaSizer->Add( m_type, 1, wxEXPAND );

        metaSizer->Add( new wxStaticText( this, wxID_ANY, _( "Default tolerance:" ) ), 0,
                        wxALIGN_CENTER_VERTICAL );
        m_defaultTolerance = new wxTextCtrl( this, wxID_ANY, wxS( "5%" ) );
        metaSizer->Add( m_defaultTolerance, 1, wxEXPAND );

        metaSizer->Add( new wxStaticText( this, wxID_ANY, _( "Package (if not a column):" ) ), 0,
                        wxALIGN_CENTER_VERTICAL );
        m_defaultPackage = new wxTextCtrl( this, wxID_ANY );
        metaSizer->Add( m_defaultPackage, 1, wxEXPAND );

        mainSizer->Add( metaSizer, 0, wxALL | wxEXPAND, FromDIP( 10 ) );

        wxStaticText* hint = new wxStaticText( this, wxID_ANY,
                _( "Assign a role to each CSV column.  'Value', 'Package' and 'Tolerance' form "
                   "the lookup key; 'BOM Field' columns supply the data filled into the BOM.  "
                   "'Show in BOM' adds the field as a column in BOM exports." ) );
        hint->Wrap( FromDIP( 640 ) );
        hint->SetFont( KIUI::GetInfoFont( this ) );
        mainSizer->Add( hint, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP( 10 ) );

        m_grid = new WX_GRID( this, wxID_ANY );
        m_grid->CreateGrid( 0, COL_COUNT );
        m_grid->SetColLabelValue( COL_CSV_HEADER, _( "CSV Column" ) );
        m_grid->SetColLabelValue( COL_SAMPLE, _( "Sample" ) );
        m_grid->SetColLabelValue( COL_ROLE, _( "Role" ) );
        m_grid->SetColLabelValue( COL_BOM_FIELD, _( "BOM Field Name" ) );
        m_grid->SetColLabelValue( COL_SHOW, _( "Show in BOM" ) );
        m_grid->SetUseNativeColLabels();
        m_grid->EnableDragRowSize( false );
        m_grid->SetSelectionMode( wxGrid::wxGridSelectRows );
        m_grid->SetMinSize( FromDIP( wxSize( 680, 280 ) ) );

        m_grid->PushEventHandler( new GRID_TRICKS( m_grid ) );

        mainSizer->Add( m_grid, 1, wxLEFT | wxRIGHT | wxEXPAND, FromDIP( 10 ) );

        wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
        buttons->AddButton( new wxButton( this, wxID_OK ) );
        buttons->AddButton( new wxButton( this, wxID_CANCEL ) );
        buttons->Realize();
        mainSizer->Add( buttons, 0, wxALL | wxEXPAND, FromDIP( 10 ) );

        SetSizer( mainSizer );

        populateGrid();

        SetupStandardButtons();
        finishDialogSettings();
    }

    ~DIALOG_PASSIVE_LIST_IMPORT() override { m_grid->PopEventHandler( true ); }

    const PASSIVE_TABLE& GetTable() const { return m_table; }
    const wxString&      GetReport() const { return m_report; }

    bool TransferDataFromWindow() override
    {
        if( !m_grid->CommitPendingChanges() )
            return false;

        return buildTable();
    }

private:
    void populateGrid()
    {
        const std::vector<wxString>& headers = m_data[0];

        m_grid->AppendRows( (int) headers.size() );

        wxArrayString roleChoices;

        for( COLUMN_ROLE role : { COLUMN_ROLE::ROLE_IGNORE, COLUMN_ROLE::ROLE_VALUE, COLUMN_ROLE::ROLE_PACKAGE,
                                  COLUMN_ROLE::ROLE_TOLERANCE, COLUMN_ROLE::ROLE_TYPE,
                                  COLUMN_ROLE::ROLE_BOM_FIELD } )
        {
            roleChoices.Add( roleName( role ) );
        }

        // Auto-seed: pick the Value column by header first, parse rate as tiebreak
        int    valueCol = -1;
        double bestScore = 0.0;

        for( size_t col = 0; col < headers.size(); ++col )
        {
            wxString norm = normalizedHeader( headers[col] );

            bool headerMatch = norm.Contains( wxS( "value" ) )
                               || norm == wxS( "resistance" ) || norm == wxS( "capacitance" )
                               || norm == wxS( "inductance" );

            if( looksLikeSerialColumn( m_data, col, 1 ) )
                continue;

            int parsed = 0;
            int total = 0;

            for( size_t row = 1; row < m_data.size() && total < 50; ++row )
            {
                if( col >= m_data[row].size() || m_data[row][col].IsEmpty() )
                    continue;

                total++;

                int64_t dummy = 0;

                if( NormalizePassiveValue( m_data[row][col], PASSIVE_TYPE::RESISTOR, &dummy ) )
                    parsed++;
            }

            if( total == 0 )
                continue;

            double score = (double) parsed / total + ( headerMatch ? 1.0 : 0.0 );

            if( score > bestScore && ( headerMatch || (double) parsed / total >= 0.6 ) )
            {
                bestScore = score;
                valueCol = (int) col;
            }
        }

        for( size_t col = 0; col < headers.size(); ++col )
        {
            wxString header = headers[col];
            wxString norm = normalizedHeader( header );
            wxString sample = ( m_data.size() > 1 && col < m_data[1].size() ) ? m_data[1][col]
                                                                              : wxString();

            COLUMN_ROLE role = COLUMN_ROLE::ROLE_IGNORE;
            wxString    bomField;
            bool        show = false;

            if( (int) col == valueCol )
            {
                role = COLUMN_ROLE::ROLE_VALUE;
            }
            else if( norm == wxS( "package" ) || norm == wxS( "case" )
                     || norm == wxS( "casecode" ) || norm == wxS( "footprint" ) )
            {
                role = COLUMN_ROLE::ROLE_PACKAGE;
            }
            else if( norm == wxS( "tolerance" ) || norm == wxS( "tol" ) )
            {
                role = COLUMN_ROLE::ROLE_TOLERANCE;
            }
            else if( norm == wxS( "type" ) || norm == wxS( "componenttype" ) )
            {
                role = COLUMN_ROLE::ROLE_TYPE;
            }
            else if( FIELDS_EDITOR_GRID_DATA_MODEL::isManufacturerFieldName( header ) )
            {
                role = COLUMN_ROLE::ROLE_BOM_FIELD;
                bomField = wxS( "Manufacturer" );
                show = true;
            }
            else if( FIELDS_EDITOR_GRID_DATA_MODEL::isMpnFieldName( header ) )
            {
                role = COLUMN_ROLE::ROLE_BOM_FIELD;
                bomField = wxS( "MPN" );
                show = true;
            }
            else if( norm == wxS( "description" ) || norm == wxS( "desc" ) )
            {
                role = COLUMN_ROLE::ROLE_BOM_FIELD;
                bomField = wxS( "Description" );
                show = true;
            }
            else if( !norm.IsEmpty() )
            {
                // Every mapped BOM field starts ticked: the data is available to the BOM,
                // and unticking is easier than discovering a hidden column.
                role = COLUMN_ROLE::ROLE_BOM_FIELD;
                bomField = header;
                show = true;
            }

            int row = (int) col;

            m_grid->SetCellValue( row, COL_CSV_HEADER, header );
            m_grid->SetReadOnly( row, COL_CSV_HEADER );

            m_grid->SetCellValue( row, COL_SAMPLE, sample );
            m_grid->SetReadOnly( row, COL_SAMPLE );

            m_grid->SetCellValue( row, COL_ROLE, roleName( role ) );
            m_grid->SetCellEditor( row, COL_ROLE, new wxGridCellChoiceEditor( roleChoices ) );

            m_grid->SetCellValue( row, COL_BOM_FIELD, bomField );

            m_grid->SetCellValue( row, COL_SHOW, show ? wxS( "1" ) : wxS( "0" ) );
            m_grid->SetCellRenderer( row, COL_SHOW, new wxGridCellBoolRenderer() );

            // Not really read-only; interactivity is delegated to GRID_TRICKS
            m_grid->SetReadOnly( row, COL_SHOW );
        }

        m_grid->SetupColumnAutosizer( COL_BOM_FIELD );
    }

    bool buildTable()
    {
        const std::vector<wxString>& headers = m_data[0];

        int valueCol = -1;
        int packageCol = -1;
        int toleranceCol = -1;
        int typeCol = -1;

        struct FIELD_COL
        {
            int      col;
            wxString name;
            bool     show;
        };

        std::vector<FIELD_COL> fieldCols;

        for( int row = 0; row < (int) headers.size(); ++row )
        {
            switch( roleFromName( m_grid->GetCellValue( row, COL_ROLE ) ) )
            {
            case COLUMN_ROLE::ROLE_VALUE:     valueCol = row;     break;
            case COLUMN_ROLE::ROLE_PACKAGE:   packageCol = row;   break;
            case COLUMN_ROLE::ROLE_TOLERANCE: toleranceCol = row; break;
            case COLUMN_ROLE::ROLE_TYPE:      typeCol = row;      break;

            case COLUMN_ROLE::ROLE_BOM_FIELD:
            {
                wxString name = m_grid->GetCellValue( row, COL_BOM_FIELD );
                name.Trim( true ).Trim( false );

                if( name.IsEmpty() )
                    break;

                bool duplicate = false;

                for( const FIELD_COL& existing : fieldCols )
                    duplicate |= ( existing.name.CmpNoCase( name ) == 0 );

                if( !duplicate )
                {
                    fieldCols.push_back(
                            { row, name, m_grid->GetCellValue( row, COL_SHOW ) == wxS( "1" ) } );
                }

                break;
            }

            default:
                break;
            }
        }

        if( valueCol < 0 )
        {
            DisplayErrorMessage( this, _( "One column must have the 'Value' role." ) );
            return false;
        }

        if( fieldCols.empty() )
        {
            DisplayErrorMessage( this, _( "At least one column must have the 'BOM Field' role." ) );
            return false;
        }

        wxString defaultPackage = m_defaultPackage->GetValue().Upper();
        defaultPackage.Trim( true ).Trim( false );

        if( packageCol < 0 && defaultPackage.IsEmpty() )
        {
            DisplayErrorMessage( this, _( "Either map a 'Package' column or enter a default "
                                          "package." ) );
            return false;
        }

        PASSIVE_TYPE dialogType = PASSIVE_TYPE::RESISTOR;

        switch( m_type->GetSelection() )
        {
        case 1: dialogType = PASSIVE_TYPE::CAPACITOR; break;
        case 2: dialogType = PASSIVE_TYPE::INDUCTOR; break;
        default: break;
        }

        m_table = PASSIVE_TABLE();
        m_table.name = m_name->GetValue();
        m_table.sourceFile = m_sourcePath;
        m_table.type = dialogType;
        m_table.defaultTolerance = NormalizeTolerance( m_defaultTolerance->GetValue() );
        m_table.importedAt = wxDateTime::Now().FormatISOCombined();

        if( m_table.name.IsEmpty() )
            m_table.name = wxFileName( m_sourcePath ).GetName();

        for( const FIELD_COL& fieldCol : fieldCols )
        {
            if( fieldCol.show )
                m_table.shownBomFields.push_back( fieldCol.name );
        }

        int imported = 0;
        int skipped = 0;
        int duplicates = 0;

        std::set<wxString> seenKeys;

        auto cellAt = []( const std::vector<wxString>& aRow, int aCol ) -> wxString
        {
            if( aCol >= 0 && aCol < (int) aRow.size() )
            {
                wxString cell = aRow[aCol];
                cell.Trim( true ).Trim( false );
                return cell;
            }

            return wxEmptyString;
        };

        for( size_t i = 1; i < m_data.size(); ++i )
        {
            const std::vector<wxString>& row = m_data[i];

            bool allEmpty = true;

            for( const wxString& cell : row )
                allEmpty &= cell.IsEmpty();

            if( allEmpty )
                continue;

            PASSIVE_TYPE rowType = dialogType;

            if( typeCol >= 0 )
            {
                rowType = TypeFromString( cellAt( row, typeCol ) );

                if( rowType == PASSIVE_TYPE::UNKNOWN )
                    rowType = dialogType;
            }

            PASSIVE_RECORD record;

            if( !NormalizePassiveValue( cellAt( row, valueCol ), rowType, &record.valueScaled ) )
            {
                skipped++;
                continue;
            }

            record.package = packageCol >= 0 ? cellAt( row, packageCol ).Upper() : defaultPackage;

            if( record.package.IsEmpty() )
            {
                skipped++;
                continue;
            }

            record.tolerance = NormalizeTolerance( cellAt( row, toleranceCol ) );

            for( const FIELD_COL& fieldCol : fieldCols )
            {
                wxString cell = cellAt( row, fieldCol.col );

                if( !cell.IsEmpty() )
                    record.fields[fieldCol.name] = cell;
            }

            if( record.fields.empty() )
            {
                skipped++;
                continue;
            }

            wxString key = wxString::Format( wxS( "%lld|%s|%s" ),
                                             (long long) record.valueScaled, record.package,
                                             record.tolerance );

            if( !seenKeys.insert( key ).second )
            {
                duplicates++;
                continue;
            }

            m_table.records.push_back( std::move( record ) );
            imported++;
        }

        if( imported == 0 )
        {
            DisplayErrorMessage( this, _( "No rows could be imported.  Check the 'Value' column "
                                          "mapping." ) );
            return false;
        }

        m_report.Printf( _( "%d parts imported, %d rows skipped, %d duplicates ignored." ),
                         imported, skipped, duplicates );

        return true;
    }

    wxString                                 m_sourcePath;
    const std::vector<std::vector<wxString>> m_data;

    PASSIVE_TABLE m_table;
    wxString      m_report;

    wxTextCtrl* m_name;
    wxChoice*   m_type;
    wxTextCtrl* m_defaultTolerance;
    wxTextCtrl* m_defaultPackage;
    WX_GRID*    m_grid;
};

} // namespace


// --------------------------------------------------------------------------------------
// The Preferences panel
// --------------------------------------------------------------------------------------

PANEL_SCH_PASSIVE_PARTS::PANEL_SCH_PASSIVE_PARTS( wxWindow* aParent ) :
        RESETTABLE_PANEL( aParent ),
        m_enableFill( nullptr ),
        m_defaultTolerance( nullptr ),
        m_tableList( nullptr ),
        m_tableEnabled( nullptr ),
        m_tableInfo( nullptr ),
        m_preview( nullptr ),
        m_importButton( nullptr ),
        m_exportButton( nullptr ),
        m_removeButton( nullptr )
{
    wxBoxSizer* topSizer = new wxBoxSizer( wxVERTICAL );

    wxStaticText* description = new wxStaticText( this, wxID_ANY,
            _( "Import your company's passive parts lists (CSV).  BOM Generation uses them to "
               "fill Manufacturer, MPN, Description and other columns for resistors, capacitors "
               "and inductors, matched by value, package and tolerance.  Values are shown in the "
               "BOM only; the schematic is never modified." ) );
    description->Wrap( FromDIP( 560 ) );
    description->SetFont( KIUI::GetInfoFont( this ) );
    topSizer->Add( description, 0, wxALL | wxEXPAND, FromDIP( 10 ) );

    wxBoxSizer* optionsSizer = new wxBoxSizer( wxHORIZONTAL );

    m_enableFill = new wxCheckBox( this, wxID_ANY, _( "Fill BOM fields from passive parts lists" ) );
    optionsSizer->Add( m_enableFill, 0, wxALIGN_CENTER_VERTICAL );

    optionsSizer->AddSpacer( FromDIP( 24 ) );

    optionsSizer->Add( new wxStaticText( this, wxID_ANY, _( "Default tolerance:" ) ), 0,
                       wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 6 ) );
    m_defaultTolerance = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                         FromDIP( wxSize( 60, -1 ) ) );
    m_defaultTolerance->SetToolTip( _( "Tolerance class assumed for symbols without a Tolerance "
                                       "field when several parts share a value and package" ) );
    optionsSizer->Add( m_defaultTolerance, 0, wxALIGN_CENTER_VERTICAL );

    topSizer->Add( optionsSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP( 10 ) );

    wxBoxSizer* listSizer = new wxBoxSizer( wxHORIZONTAL );

    m_tableList = new wxListBox( this, wxID_ANY );
    m_tableList->SetMinSize( FromDIP( wxSize( 220, 140 ) ) );
    listSizer->Add( m_tableList, 1, wxEXPAND );

    wxBoxSizer* buttonSizer = new wxBoxSizer( wxVERTICAL );

    m_importButton = new wxButton( this, wxID_ANY, _( "Import..." ) );
    buttonSizer->Add( m_importButton, 0, wxBOTTOM | wxEXPAND, FromDIP( 4 ) );

    m_exportButton = new wxButton( this, wxID_ANY, _( "Export..." ) );
    buttonSizer->Add( m_exportButton, 0, wxBOTTOM | wxEXPAND, FromDIP( 4 ) );

    m_removeButton = new wxButton( this, wxID_ANY, _( "Remove" ) );
    buttonSizer->Add( m_removeButton, 0, wxBOTTOM | wxEXPAND, FromDIP( 4 ) );

    m_tableEnabled = new wxCheckBox( this, wxID_ANY, _( "Enabled" ) );
    buttonSizer->Add( m_tableEnabled, 0, wxTOP, FromDIP( 8 ) );

    listSizer->Add( buttonSizer, 0, wxLEFT, FromDIP( 8 ) );

    topSizer->Add( listSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP( 10 ) );

    m_tableInfo = new wxStaticText( this, wxID_ANY, wxEmptyString );
    m_tableInfo->SetFont( KIUI::GetSmallInfoFont( this ).Italic() );
    topSizer->Add( m_tableInfo, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP( 10 ) );

    m_preview = new WX_GRID( this, wxID_ANY );
    m_preview->CreateGrid( 0, 1 );
    m_preview->SetUseNativeColLabels();
    m_preview->EnableEditing( false );
    m_preview->EnableDragRowSize( false );
    m_preview->SetMinSize( FromDIP( wxSize( -1, 200 ) ) );
    topSizer->Add( m_preview, 1, wxALL | wxEXPAND, FromDIP( 10 ) );

    SetSizer( topSizer );

    m_importButton->Bind( wxEVT_BUTTON, &PANEL_SCH_PASSIVE_PARTS::onImport, this );
    m_exportButton->Bind( wxEVT_BUTTON, &PANEL_SCH_PASSIVE_PARTS::onExport, this );
    m_removeButton->Bind( wxEVT_BUTTON, &PANEL_SCH_PASSIVE_PARTS::onRemove, this );
    m_tableList->Bind( wxEVT_LISTBOX, &PANEL_SCH_PASSIVE_PARTS::onSelectTable, this );
    m_tableEnabled->Bind( wxEVT_CHECKBOX, &PANEL_SCH_PASSIVE_PARTS::onToggleEnabled, this );
}


PANEL_SCH_PASSIVE_PARTS::~PANEL_SCH_PASSIVE_PARTS()
{
}


bool PANEL_SCH_PASSIVE_PARTS::TransferDataToWindow()
{
    if( EESCHEMA_SETTINGS* cfg = GetAppSettings<EESCHEMA_SETTINGS>( "eeschema" ) )
    {
        m_enableFill->SetValue( cfg->m_PassiveParts.enable );
        m_defaultTolerance->SetValue( cfg->m_PassiveParts.default_tolerance );
    }

    reloadTables();
    return true;
}


bool PANEL_SCH_PASSIVE_PARTS::TransferDataFromWindow()
{
    if( EESCHEMA_SETTINGS* cfg = GetAppSettings<EESCHEMA_SETTINGS>( "eeschema" ) )
    {
        cfg->m_PassiveParts.enable = m_enableFill->GetValue();
        cfg->m_PassiveParts.default_tolerance =
                NormalizeTolerance( m_defaultTolerance->GetValue() );
    }

    // Persist enabled-flag changes
    for( size_t i = 0; i < m_tables.size(); ++i )
    {
        if( m_enabledDirty[i] )
            PASSIVE_TABLE_STORE::Get().Save( m_tables[i] );
    }

    return true;
}


void PANEL_SCH_PASSIVE_PARTS::ResetPanel()
{
    PASSIVE_TABLE_STORE::Get().Reload();
    reloadTables();
}


void PANEL_SCH_PASSIVE_PARTS::reloadTables( const wxString& aSelectId )
{
    m_tables = PASSIVE_TABLE_STORE::Get().Tables();
    m_enabledDirty.assign( m_tables.size(), false );

    m_tableList->Clear();

    int selection = m_tables.empty() ? wxNOT_FOUND : 0;

    for( size_t i = 0; i < m_tables.size(); ++i )
    {
        const PASSIVE_TABLE& table = m_tables[i];

        wxString label = wxString::Format( wxS( "%s  (%d %s)" ), table.name,
                                           (int) table.records.size(),
                                           TypeToString( table.type ) );

        if( !table.enabled )
            label += _( "  [disabled]" );

        m_tableList->Append( label );

        if( !aSelectId.IsEmpty() && table.id == aSelectId )
            selection = (int) i;
    }

    if( selection != wxNOT_FOUND )
        m_tableList->SetSelection( selection );

    updateSelection();
}


PASSIVE_PARTS::PASSIVE_TABLE* PANEL_SCH_PASSIVE_PARTS::selectedTable()
{
    int selection = m_tableList->GetSelection();

    if( selection == wxNOT_FOUND || selection >= (int) m_tables.size() )
        return nullptr;

    return &m_tables[selection];
}


void PANEL_SCH_PASSIVE_PARTS::updateSelection()
{
    PASSIVE_TABLE* table = selectedTable();

    m_exportButton->Enable( table != nullptr );
    m_removeButton->Enable( table != nullptr );
    m_tableEnabled->Enable( table != nullptr );

    if( table )
    {
        m_tableEnabled->SetValue( table->enabled );

        wxString info = wxString::Format( _( "Imported %s from %s" ), table->importedAt,
                                          table->sourceFile );

        if( !table->defaultTolerance.IsEmpty() )
            info += wxString::Format( _( "  (default tolerance %s)" ), table->defaultTolerance );

        m_tableInfo->SetLabel( info );
    }
    else
    {
        m_tableEnabled->SetValue( false );
        m_tableInfo->SetLabel( m_tables.empty() ? _( "No passive parts lists imported yet." )
                                                : wxString() );
    }

    updatePreview();
}


void PANEL_SCH_PASSIVE_PARTS::updatePreview()
{
    m_preview->Freeze();
    m_preview->ClearGrid();

    if( m_preview->GetNumberRows() > 0 )
        m_preview->DeleteRows( 0, m_preview->GetNumberRows() );

    if( m_preview->GetNumberCols() > 0 )
        m_preview->DeleteCols( 0, m_preview->GetNumberCols() );

    PASSIVE_TABLE* table = selectedTable();

    if( !table || table->records.empty() )
    {
        m_preview->AppendCols( 1 );
        m_preview->SetColLabelValue( 0, _( "Preview" ) );
        m_preview->Thaw();
        return;
    }

    // Columns: the key plus the union of field names over the previewed records
    std::vector<wxString> fieldNames;

    for( const PASSIVE_RECORD& record : table->records )
    {
        for( const auto& [name, value] : record.fields )
        {
            if( std::find( fieldNames.begin(), fieldNames.end(), name ) == fieldNames.end() )
                fieldNames.push_back( name );
        }
    }

    const int previewLimit = 200;
    int       rows = std::min( (int) table->records.size(), previewLimit );

    m_preview->AppendCols( 3 + (int) fieldNames.size() );
    m_preview->SetColLabelValue( 0, _( "Value" ) );
    m_preview->SetColLabelValue( 1, _( "Package" ) );
    m_preview->SetColLabelValue( 2, _( "Tolerance" ) );

    for( size_t i = 0; i < fieldNames.size(); ++i )
        m_preview->SetColLabelValue( 3 + (int) i, fieldNames[i] );

    m_preview->AppendRows( rows );

    for( int row = 0; row < rows; ++row )
    {
        const PASSIVE_RECORD& record = table->records[row];

        double value = (double) record.valueScaled / (double) ValueScaleForType( table->type );

        m_preview->SetCellValue( row, 0, wxString( SIM_VALUE::Normalize( value ) ) );
        m_preview->SetCellValue( row, 1, record.package );
        m_preview->SetCellValue( row, 2, record.tolerance );

        for( size_t i = 0; i < fieldNames.size(); ++i )
        {
            auto it = record.fields.find( fieldNames[i] );

            if( it != record.fields.end() )
                m_preview->SetCellValue( row, 3 + (int) i, it->second );
        }
    }

    m_preview->AutoSizeColumns( false );
    m_preview->Thaw();
}


void PANEL_SCH_PASSIVE_PARTS::onImport( wxCommandEvent& aEvent )
{
    wxFileDialog dlg( this, _( "Select passive parts list" ), wxEmptyString, wxEmptyString,
                      FILEEXT::CsvTsvFileWildcard(), wxFD_OPEN | wxFD_FILE_MUST_EXIST );

    KIPLATFORM::UI::AllowNetworkFileSystems( &dlg );

    if( dlg.ShowModal() == wxID_CANCEL )
        return;

    wxString content;

    try
    {
        content = SafeReadFile( dlg.GetPath(), wxS( "r" ) );
    }
    catch( const IO_ERROR& ioe )
    {
        DisplayErrorMessage( this, ioe.What() );
        return;
    }

    std::vector<std::vector<wxString>> data;

    if( !AutoDecodeCSV( content, data ) || data.size() < 2 )
    {
        DisplayErrorMessage( this, _( "The file could not be read as CSV/TSV, or contains no "
                                      "data rows." ) );
        return;
    }

    DIALOG_PASSIVE_LIST_IMPORT importDlg( this, dlg.GetPath(), data );

    if( importDlg.ShowModal() != wxID_OK )
        return;

    PASSIVE_TABLE table = importDlg.GetTable();

    int maxOrder = -1;

    for( const PASSIVE_TABLE& existing : m_tables )
        maxOrder = std::max( maxOrder, existing.order );

    table.order = maxOrder + 1;

    if( !PASSIVE_TABLE_STORE::Get().Save( table ) )
    {
        DisplayErrorMessage( this, _( "The parts list could not be saved." ) );
        return;
    }

    reloadTables( table.id );

    wxMessageBox( importDlg.GetReport(), _( "Import Passive Parts List" ), wxOK | wxICON_INFORMATION,
                  this );
}


void PANEL_SCH_PASSIVE_PARTS::onExport( wxCommandEvent& aEvent )
{
    PASSIVE_TABLE* table = selectedTable();

    if( !table )
        return;

    wxFileName fn( table->name + wxS( ".csv" ) );

    wxFileDialog dlg( this, _( "Export passive parts list" ), wxEmptyString, fn.GetFullName(),
                      FILEEXT::CsvFileWildcard(), wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    KIPLATFORM::UI::AllowNetworkFileSystems( &dlg );

    if( dlg.ShowModal() == wxID_CANCEL )
        return;

    std::vector<wxString> fieldNames;

    for( const PASSIVE_RECORD& record : table->records )
    {
        for( const auto& [name, value] : record.fields )
        {
            if( std::find( fieldNames.begin(), fieldNames.end(), name ) == fieldNames.end() )
                fieldNames.push_back( name );
        }
    }

    std::vector<std::vector<wxString>> rows;

    std::vector<wxString> header = { _( "Value" ), _( "Package" ), _( "Tolerance" ) };
    header.insert( header.end(), fieldNames.begin(), fieldNames.end() );
    rows.push_back( header );

    for( const PASSIVE_RECORD& record : table->records )
    {
        double value = (double) record.valueScaled / (double) ValueScaleForType( table->type );

        std::vector<wxString> row = { wxString( SIM_VALUE::Normalize( value ) ), record.package,
                                      record.tolerance };

        for( const wxString& name : fieldNames )
        {
            auto it = record.fields.find( name );
            row.push_back( it != record.fields.end() ? it->second : wxString() );
        }

        rows.push_back( std::move( row ) );
    }

    wxFFileOutputStream stream( dlg.GetPath() );

    if( !stream.IsOk() )
    {
        DisplayErrorMessage( this, _( "The file could not be written." ) );
        return;
    }

    CSV_WRITER writer( stream );
    writer.WriteLines( rows );
}


void PANEL_SCH_PASSIVE_PARTS::onRemove( wxCommandEvent& aEvent )
{
    PASSIVE_TABLE* table = selectedTable();

    if( !table )
        return;

    wxString msg = wxString::Format( _( "Remove passive parts list '%s'?" ), table->name );

    if( wxMessageBox( msg, _( "Remove Passive Parts List" ), wxYES_NO | wxICON_QUESTION, this )
            != wxYES )
    {
        return;
    }

    PASSIVE_TABLE_STORE::Get().Remove( table->id );
    reloadTables();
}


void PANEL_SCH_PASSIVE_PARTS::onSelectTable( wxCommandEvent& aEvent )
{
    updateSelection();
}


void PANEL_SCH_PASSIVE_PARTS::onToggleEnabled( wxCommandEvent& aEvent )
{
    int selection = m_tableList->GetSelection();

    if( selection == wxNOT_FOUND || selection >= (int) m_tables.size() )
        return;

    m_tables[selection].enabled = m_tableEnabled->GetValue();
    m_enabledDirty[selection] = true;

    wxString label = wxString::Format( wxS( "%s  (%d %s)" ), m_tables[selection].name,
                                       (int) m_tables[selection].records.size(),
                                       TypeToString( m_tables[selection].type ) );

    if( !m_tables[selection].enabled )
        label += _( "  [disabled]" );

    m_tableList->SetString( selection, label );
}
