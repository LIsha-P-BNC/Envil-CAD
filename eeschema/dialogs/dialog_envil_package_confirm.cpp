/*
 * This program source code file is part of Envil.
 */

#include "dialog_envil_package_confirm.h"

#include <sch_base_frame.h>
#include <widgets/footprint_select_widget.h>

#include <wx/button.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>


DIALOG_ENVIL_PACKAGE_CONFIRM::DIALOG_ENVIL_PACKAGE_CONFIRM( SCH_BASE_FRAME* aParent,
                                                             const LIB_ID& aLibId,
                                                             const wxString& aSymbolName,
                                                             const wxArrayString& aFpFilters,
                                                             const wxString& aDefaultFootprint ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Confirm Component Package" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_defaultFootprint( aDefaultFootprint ),
        m_packagePref( PACKAGE_PREF::THT )
{
    // Heuristic seed only -- KiCad's own stock libraries consistently suffix the
    // library nickname with _SMD/_THT (Resistor_SMD vs Resistor_THT, Package_TO_SOT_SMD
    // vs Package_TO_SOT_THT, ...). The user's own choice below is authoritative.
    if( aDefaultFootprint.Upper().Contains( wxT( "SMD" ) ) )
        m_packagePref = PACKAGE_PREF::SMD;

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    wxString prompt = wxString::Format( _( "Choose the package and library footprint for '%s' "
                                            "(%s) before it is placed." ),
                                         aSymbolName, aLibId.GetUniStringLibId() );
    wxStaticText* promptText = new wxStaticText( this, wxID_ANY, prompt );
    promptText->Wrap( 420 );
    mainSizer->Add( promptText, 0, wxALL | wxEXPAND, 10 );

    wxString packageChoices[] = { _( "Through-Hole (THT)" ), _( "Surface-Mount (SMD)" ) };
    m_packageTypeCtrl = new wxRadioBox( this, wxID_ANY, _( "Package Type" ), wxDefaultPosition,
                                        wxDefaultSize, 2, packageChoices, 1, wxRA_SPECIFY_ROWS );
    m_packageTypeCtrl->SetSelection( m_packagePref == PACKAGE_PREF::SMD ? 1 : 0 );
    mainSizer->Add( m_packageTypeCtrl, 0, wxLEFT | wxRIGHT | wxEXPAND, 10 );

    mainSizer->Add( new wxStaticLine( this ), 0, wxALL | wxEXPAND, 8 );

    wxStaticText* libLabel = new wxStaticText( this, wxID_ANY,
                                                _( "Footprint (from your configured libraries):" ) );
    mainSizer->Add( libLabel, 0, wxLEFT | wxRIGHT | wxTOP, 10 );

    m_fpSelect = new FOOTPRINT_SELECT_WIDGET( aParent, this );
    m_fpSelect->Load( aParent->Kiway(), aParent->Prj() );
    m_fpSelect->FilterByFootprintFilters( aFpFilters, false );
    m_fpSelect->SetDefaultFootprint( aDefaultFootprint );
    m_fpSelect->UpdateList();
    mainSizer->Add( m_fpSelect, 0, wxALL | wxEXPAND, 10 );

    wxStdDialogButtonSizer* sdbSizer = new wxStdDialogButtonSizer();
    sdbSizer->AddButton( new wxButton( this, wxID_OK ) );
    sdbSizer->AddButton( new wxButton( this, wxID_CANCEL ) );
    sdbSizer->Realize();
    mainSizer->Add( sdbSizer, 0, wxALL | wxALIGN_RIGHT, 10 );

    SetSizer( mainSizer );

    m_packageTypeCtrl->Bind( wxEVT_RADIOBOX, &DIALOG_ENVIL_PACKAGE_CONFIRM::onPackageTypeChanged,
                              this );
    m_fpSelect->Bind( EVT_FOOTPRINT_SELECTED, &DIALOG_ENVIL_PACKAGE_CONFIRM::onFootprintSelected,
                       this );

    SetupStandardButtons();
    Layout();
    Fit();
}


void DIALOG_ENVIL_PACKAGE_CONFIRM::onPackageTypeChanged( wxCommandEvent& aEvent )
{
    m_packagePref = ( m_packageTypeCtrl->GetSelection() == 1 ) ? PACKAGE_PREF::SMD
                                                                : PACKAGE_PREF::THT;
}


void DIALOG_ENVIL_PACKAGE_CONFIRM::onFootprintSelected( wxCommandEvent& aEvent )
{
    m_selectedFootprint = aEvent.GetString();
}
