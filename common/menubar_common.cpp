/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * @file menubar_common.cpp
 *
 * KiCad Next: the shared "common" top menu-bar assembler.
 *
 * Instead of every frame building its own menu bar from scratch in doReCreateMenuBar(), the
 * unified path builds a single canonical menu bar (File, Edit, View, Place, Route, Inspect,
 * Tools, Preferences, Help) and lets each frame populate only the menus it owns through the
 * virtual build*Menu() hooks declared on EDA_BASE_FRAME.  A menu that a frame does not fill is
 * skipped, so the same builder yields the schematic editor's full menu set, the PCB editor's
 * Route menu, the Gerber viewer's no-Edit/no-Place bar, and the calculator's File-only bar.
 *
 * This file is additive: it is reached only when ADVANCED_CFG m_UnifiedMenuBar is set, and the
 * legacy per-frame doReCreateMenuBar() bodies remain the default code path.
 */

#include <eda_base_frame.h>
#include <tool/action_menu.h>
#include <tool/tool_interactive.h>
#include <widgets/wx_menubar.h>

#include <wx/menu.h>


void EDA_BASE_FRAME::buildCommonMenuBar()
{
    // Build this frame's menu from its own hooks (the original behaviour).
    buildCommonMenuBarFrom( this );
}


void EDA_BASE_FRAME::buildCommonMenuBarFrom( EDA_BASE_FRAME* aSource )
{
    wxCHECK_RET( aSource, wxT( "buildCommonMenuBarFrom() requires a non-null source frame" ) );

    TOOL_INTERACTIVE* tool = aSource->getCurrentMenuTool();

    // The unified path needs a tool to dispatch menu events.  A frame that opts in via the flag
    // is expected to override getCurrentMenuTool(); guard rather than crash if it does not.
    wxCHECK_RET( tool, wxT( "buildCommonMenuBarFrom() requires a non-null getCurrentMenuTool()" ) );

    // wxWidgets handles the Mac Application menu behind the scenes, but that means we always have
    // to start from scratch with a new wxMenuBar (mirrors the legacy doReCreateMenuBar() bodies).
    wxMenuBar*  oldMenuBar = GetMenuBar();
    WX_MENUBAR* menuBar    = new WX_MENUBAR();

    auto appendIfNonEmpty =
            [&]( const wxString& aTitle, void ( EDA_BASE_FRAME::*aBuilder )( ACTION_MENU* ) )
            {
                ACTION_MENU* menu = new ACTION_MENU( false, tool );

                // Fill from the SOURCE frame's hook (== this for the plain buildCommonMenuBar()
                // case); the menu's tool is the source frame's, so its events route there.
                ( aSource->*aBuilder )( menu );

                // A frame opts into a top-level menu simply by adding items in its hook; an empty
                // menu means "this frame has no such menu" and is dropped.
                if( menu->GetMenuItemCount() > 0 )
                    menuBar->Append( menu, aTitle );
                else
                    delete menu;
            };

    appendIfNonEmpty( _( "&File" ),        &EDA_BASE_FRAME::buildFileMenu );
    appendIfNonEmpty( _( "&Edit" ),        &EDA_BASE_FRAME::buildEditMenu );
    appendIfNonEmpty( _( "&View" ),        &EDA_BASE_FRAME::buildViewMenu );
    appendIfNonEmpty( _( "&Place" ),       &EDA_BASE_FRAME::buildPlaceMenu );
    appendIfNonEmpty( _( "Ro&ute" ),       &EDA_BASE_FRAME::buildRouteMenu );
    appendIfNonEmpty( _( "&Inspect" ),     &EDA_BASE_FRAME::buildInspectMenu );
    appendIfNonEmpty( _( "&Tools" ),       &EDA_BASE_FRAME::buildToolsMenu );
    appendIfNonEmpty( _( "P&references" ), &EDA_BASE_FRAME::buildPreferencesMenu );

    // Help is identical for every frame and already shared.
    aSource->AddStandardHelpMenu( menuBar );

    SetMenuBar( menuBar );
    delete oldMenuBar;
}
