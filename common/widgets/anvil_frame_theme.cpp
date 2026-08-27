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

#include <widgets/anvil_frame_theme.h>

#include <eda_base_frame.h>
#include <kiplatform/anvil_theme.h>
#include <tool/action_toolbar.h>
#include <widgets/properties_panel.h>
#include <widgets/wx_aui_art_providers.h>
#include <widgets/wx_infobar.h>

#include <wx/window.h>

namespace
{
void anvilRecolorSubtree( wxWindow* aWindow, const std::vector<wxWindow*>& aExclude )
{
    if( !aWindow )
        return;

    for( wxWindow* skip : aExclude )
    {
        if( aWindow == skip )
            return;
    }

    aWindow->SetBackgroundColour( ANVIL::CHROME_PANEL );
    aWindow->SetForegroundColour( ANVIL::BONE );

    // A few widgets need more than a background/foreground pair because they COPY colours into
    // surfaces of their own that SetBackgroundColour never reaches.  The walk already visits
    // every child, so it is the natural place to give them their second chance.
    if( PROPERTIES_PANEL* props = dynamic_cast<PROPERTIES_PANEL*>( aWindow ) )
        props->ApplyAnvilTheme();

    for( wxWindow* child : aWindow->GetChildren() )
        anvilRecolorSubtree( child, aExclude );
}
} // namespace


void KIUI::ApplyAnvilFrameTheme( EDA_BASE_FRAME* aFrame, const std::vector<wxWindow*>& aExtraExclude )
{
    if( !aFrame )
        return;

    // Remember what this frame wants skipped so EDA_BASE_FRAME::ReapplyAnvilTheme() can replay
    // exactly this call when the user flips the light/dark toggle -- without every editor having
    // to duplicate its exclusion list in an override.
    aFrame->SetAnvilThemeExclusions( aExtraExclude );

    // 1) Dock-pane chrome: flat uniform captions are drawn by WX_AUI_DOCK_ART::DrawCaption; the
    //    colours here cover the background, sashes, borders and gripper.
    if( wxAuiDockArt* dockArt = aFrame->GetAuiManager().GetArtProvider() )
    {
        dockArt->SetColour( wxAUI_DOCKART_BACKGROUND_COLOUR, ANVIL::CONTENT );
        dockArt->SetColour( wxAUI_DOCKART_SASH_COLOUR, ANVIL::CHROME_SASH );
        dockArt->SetColour( wxAUI_DOCKART_BORDER_COLOUR, ANVIL::CHROME_LINE );
        dockArt->SetColour( wxAUI_DOCKART_GRIPPER_COLOUR, ANVIL::CHROME_HEADER );
        dockArt->SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, ANVIL::CHROME_HEADER );
        dockArt->SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_GRADIENT_COLOUR, ANVIL::CHROME_HEADER );
        dockArt->SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, ANVIL::CHROME_HEADER );
        dockArt->SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_GRADIENT_COLOUR, ANVIL::CHROME_HEADER );
        dockArt->SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, ANVIL::BONE );
        dockArt->SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, ANVIL::BONE );
    }

    // 2) AUI tool-bars: bar background + hover/pressed/checked highlight (mono icons + emerald
    //    hover come from WX_AUI_TOOLBAR_ART itself).  Includes the Active Bar -- it is not one
    //    of the four standard toolbar members, so a frame with drawing tools would otherwise
    //    keep the stock fill on that one row.
    //
    // Two bar tiers, because the light mockup splits them.  DARK (Deep Emerald): the top main
    // tool-bar and the two vertical rails.  LIGHT (cream): the AUX row — the Track / Via /
    // layer / Grid / Zoom value controls — and the drawing-tools Active Bar.  Both light rows
    // read as part of the content rather than as chrome, so the window shows ONE emerald band
    // (menu + main tool-bar) and then steps down cream -> white instead of stacking three
    // emerald rows on top of each other.  In the dark theme CHROME_BAR == CHROME_BAR2, so this
    // split is a no-op there.
    ACTION_TOOLBAR* auxBar    = aFrame->GetTopAuxToolbar();
    ACTION_TOOLBAR* activeBar = aFrame->GetActiveBarToolbar();

    for( ACTION_TOOLBAR* tb : { aFrame->GetTopMainToolbar(), auxBar,
                                aFrame->GetLeftToolbar(), aFrame->GetRightToolbar(),
                                activeBar } )
    {
        if( !tb )
            continue;

        const bool     darkBar = ( tb != auxBar && tb != activeBar );
        const wxColour barBg   = darkBar ? ANVIL::CHROME_BAR : ANVIL::CHROME_BAR2;

        if( WX_AUI_TOOLBAR_ART* art = dynamic_cast<WX_AUI_TOOLBAR_ART*>( tb->GetArtProvider() ) )
            art->EnableAnvilTheme( barBg, ANVIL::ACCENT, darkBar );

        tb->SetBackgroundColour( barBg );
        tb->Refresh();
    }

    // 3) Every other child control (panels, trees, lists, message panel, status bar...).  The
    //    info bar is always excluded -- it has its own message-type colour scheme -- plus
    //    whatever the caller passes (the drawing canvas, an embedded web/AI panel, ...).
    std::vector<wxWindow*> exclude = aExtraExclude;

    if( aFrame->GetInfoBar() )
        exclude.push_back( aFrame->GetInfoBar() );

    for( wxWindow* child : aFrame->GetChildren() )
        anvilRecolorSubtree( child, exclude );

    aFrame->Refresh();
}
