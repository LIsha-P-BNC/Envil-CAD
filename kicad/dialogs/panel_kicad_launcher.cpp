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

#include <bitmaps.h>
#include <bitmap_store.h>
#include <kicad_manager_frame.h>
#include <kiplatform/policy.h>
#include <policy_keys.h>
#include <tool/tool_manager.h>
#include <tools/kicad_manager_actions.h>
#include <tools/kicad_manager_control.h>
#include <widgets/bitmap_button.h>
#include <wx/stattext.h>

#include "panel_kicad_launcher.h"


PANEL_KICAD_LAUNCHER::PANEL_KICAD_LAUNCHER( wxWindow* aParent ) :
        PANEL_KICAD_LAUNCHER_BASE( aParent ),
        m_frame( static_cast<KICAD_MANAGER_FRAME*>( aParent->GetParent() ) )
{
    CreateLaunchers();

    Bind( wxEVT_SYS_COLOUR_CHANGED, wxSysColourChangedEventHandler( PANEL_KICAD_LAUNCHER::onThemeChanged ), this );
}


PANEL_KICAD_LAUNCHER::~PANEL_KICAD_LAUNCHER()
{
    m_frame->SetPcmButton( nullptr );

    for( wxWindow* window : m_scrolledWindow->GetChildren() )
    {
        if( dynamic_cast<BITMAP_BUTTON*>( window ) != nullptr )
            window->Unbind( wxEVT_BUTTON, &PANEL_KICAD_LAUNCHER::onLauncherButtonClick, this );
    }

    Unbind( wxEVT_SYS_COLOUR_CHANGED, wxSysColourChangedEventHandler( PANEL_KICAD_LAUNCHER::onThemeChanged ), this );
}


void PANEL_KICAD_LAUNCHER::onLauncherButtonClick( wxCommandEvent& aEvent )
{
    // Don't accept clicks processed during wxProgressReporter updating.  In particular, the wxSafeYield()
    // call below will puke.
    if( m_frame->GetToolManager()->GetTool<KICAD_MANAGER_CONTROL>()->InShowPlayer() )
        return;

    // Defocus the button because leaving the large buttons focused after a click looks out of place in
    // the launcher
    m_frame->SetFocus();

    // Gives a slice of time to update the button state (mandatory on GTK, useful on MSW to avoid some
    // cosmetic issues).
    wxSafeYield();

    BITMAP_BUTTON*     button = (BITMAP_BUTTON*) aEvent.GetEventObject();
    const TOOL_ACTION* action = static_cast<const TOOL_ACTION*>( button->GetClientData() );

    if( action == nullptr )
        return;

    OPT_TOOL_EVENT evt = action->MakeEvent();
    evt->SetHasPosition( false );
    m_frame->GetToolManager()->ProcessEvent( *evt );
}


void PANEL_KICAD_LAUNCHER::CreateLaunchers()
{
    m_frame->SetPcmButton( nullptr );

    if( m_toolsSizer->GetEffectiveRowsCount() > 0 )
    {
        m_toolsSizer->Clear( true );
        m_toolsSizer->SetRows( 0 );
    }

    // KiCad Next: lay the 9 tools out as a single-column compact icon+label rail
    // (matching the web prototype's activity rail) instead of the 2-column
    // big-button + help-paragraph list.
    m_toolsSizer->SetCols( 1 );

    auto addLauncher =
            [&]( const TOOL_ACTION& aAction, BITMAPS aBitmaps, const wxString& aHelpText, bool enabled = true )
            {
                // Small activity-bar icons (compact rail). Force a 24 px default glyph
                // (Def() resamples down) so the rail stays slim like VS Code's bar.
                BITMAP_BUTTON* btn = new BITMAP_BUTTON( m_scrolledWindow, wxID_ANY );
                btn->SetBitmap( KiBitmapBundleDef( aBitmaps, 24 ) );
                btn->SetDisabledBitmap( KiDisabledBitmapBundleDef( aBitmaps, 24 ) );
                btn->SetPadding( 4 );

                // Icon-only activity rail: the tool name lives in the tooltip (the rail is
                // narrow, like the web prototype's activity bar). No duplicate text label.
                btn->SetToolTip( aAction.GetFriendlyName() + wxT( "\n" ) + aHelpText );

                btn->Bind( wxEVT_BUTTON, &PANEL_KICAD_LAUNCHER::onLauncherButtonClick, this );
                btn->SetClientData( (void*) &aAction );

                m_toolsSizer->Add( btn, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 4 );

                btn->Enable( enabled );

                return btn;
            };

    addLauncher( KICAD_MANAGER_ACTIONS::editSchematic, BITMAPS::icon_eeschema,
                 _( "Edit the project schematic" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::editSymbols, BITMAPS::icon_libedit,
                 _( "Edit global and/or project schematic symbol libraries" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::editPCB, BITMAPS::icon_pcbnew,
                 _( "Edit the project PCB design" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::editFootprints, BITMAPS::icon_modedit,
                 _( "Edit global and/or project PCB footprint libraries" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::viewGerbers, BITMAPS::icon_gerbview,
                 _( "Preview Gerber files" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::convertImage, BITMAPS::icon_bitmap2component,
                 _( "Convert bitmap images to schematic symbols or PCB footprints" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::showCalculator, BITMAPS::icon_pcbcalculator,
                 _( "Show tools for calculating resistance, current capacity, etc." ) );

    addLauncher( KICAD_MANAGER_ACTIONS::editDrawingSheet, BITMAPS::icon_pagelayout_editor,
                 _( "Edit drawing sheet borders and title blocks for use in schematics and PCB designs" ) );

    BITMAP_BUTTON* bb = addLauncher( KICAD_MANAGER_ACTIONS::showPluginManager, BITMAPS::icon_pcm,
                                     _( "Manage downloadable packages from KiCad and 3rd party repositories" ),
                                     KIPLATFORM::POLICY::GetPolicyBool( POLICY_KEY_PCM )
                                            != KIPLATFORM::POLICY::PBOOL::DISABLED );

    m_frame->SetPcmButton( bb );

    Layout();
}


void PANEL_KICAD_LAUNCHER::onThemeChanged( wxSysColourChangedEvent& aEvent )
{
    GetBitmapStore()->ThemeChanged();
    CreateLaunchers();

    aEvent.Skip();
}
