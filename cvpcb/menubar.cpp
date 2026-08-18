/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright (C) 2004 Jean-Pierre Charras, jaen-pierre.charras@gipsa-lab.inpg.com
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

#include <bitmaps.h>
#include <tool/actions.h>
#include <tool/common_control.h>
#include <tool/conditional_menu.h>
#include <tool/tool_manager.h>

#include <cvpcb_mainframe.h>
#include <tools/cvpcb_actions.h>
#include <widgets/wx_menubar.h>


void CVPCB_MAINFRAME::doReCreateMenuBar()
{
    // Anvil Next: build the shared common menu bar instead of the legacy one when enabled.
    if( UseUnifiedMenuBar() )
    {
        buildCommonMenuBar();
        return;
    }

    COMMON_CONTROL* tool = m_toolManager->GetTool<COMMON_CONTROL>();

    // wxWidgets handles the Mac Application menu behind the scenes, but that means
    // we always have to start from scratch with a new wxMenuBar.
    wxMenuBar*  oldMenuBar = GetMenuBar();
    WX_MENUBAR* menuBar    = new WX_MENUBAR();

    //-- File menu -----------------------------------------------------------
    //
    ACTION_MENU*   fileMenu = new ACTION_MENU( false, tool );

    fileMenu->Add( CVPCB_ACTIONS::saveAssociationsToSchematic );
    fileMenu->AppendSeparator();
    fileMenu->AddClose( _( "Assign Footprints" ) );

    //-- Edit menu -----------------------------------------------------------
    //
    ACTION_MENU* editMenu = new ACTION_MENU( false, tool );

    editMenu->Add( ACTIONS::undo );
    editMenu->Add( ACTIONS::redo );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut );
    editMenu->Add( ACTIONS::copy );
    editMenu->Add( ACTIONS::paste );

    //-- Preferences menu ----------------------------------------------------
    //
    ACTION_MENU* prefsMenu = new ACTION_MENU( false, tool );

    prefsMenu->Add( ACTIONS::configurePaths );
    prefsMenu->Add( ACTIONS::showFootprintLibTable );
    prefsMenu->Add( CVPCB_ACTIONS::showEquFileTable);
    prefsMenu->Add( ACTIONS::openPreferences);

    prefsMenu->AppendSeparator();
    AddMenuLanguageList( prefsMenu, tool );

    //-- Menubar -------------------------------------------------------------
    //
    menuBar->Append( fileMenu, _( "&File" ) );
    menuBar->Append( editMenu, _( "&Edit" ) );
    menuBar->Append( prefsMenu, _( "&Preferences" ) );
    AddStandardHelpMenu( menuBar );

    SetMenuBar( menuBar );
    delete oldMenuBar;
}


//================================ Anvil Next unified menu bar ================================
// The hooks reproduce the menus above so CvPcb joins the unified bar and the modern layout's
// Window menu; in the modern layout the Preferences items fold into a Tools tail.

TOOL_INTERACTIVE* CVPCB_MAINFRAME::getCurrentMenuTool()
{
    return m_toolManager->GetTool<COMMON_CONTROL>();
}


void CVPCB_MAINFRAME::buildFileMenu( ACTION_MENU* fileMenu )
{
    fileMenu->Add( CVPCB_ACTIONS::saveAssociationsToSchematic );
    fileMenu->AppendSeparator();
    fileMenu->AddClose( _( "Assign Footprints" ) );
}


void CVPCB_MAINFRAME::buildEditMenu( ACTION_MENU* editMenu )
{
    editMenu->Add( ACTIONS::undo );
    editMenu->Add( ACTIONS::redo );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut );
    editMenu->Add( ACTIONS::copy );
    editMenu->Add( ACTIONS::paste );
}


void CVPCB_MAINFRAME::buildToolsMenu( ACTION_MENU* toolsMenu )
{
    // The classic CvPcb has no Tools menu; the modern (Altium-style) layout gains one holding
    // the Preferences items folded in from the dropped Preferences menu.
    if( !UseModernMenuLayout() )
        return;

    COMMON_CONTROL* tool = m_toolManager->GetTool<COMMON_CONTROL>();

    toolsMenu->Add( ACTIONS::configurePaths );
    toolsMenu->Add( ACTIONS::showFootprintLibTable );
    toolsMenu->Add( CVPCB_ACTIONS::showEquFileTable );
    toolsMenu->Add( ACTIONS::openPreferences );

    toolsMenu->AppendSeparator();
    AddMenuLanguageList( toolsMenu, tool );
}


void CVPCB_MAINFRAME::buildPreferencesMenu( ACTION_MENU* prefsMenu )
{
    // Modern layout: these items live in the tail of Tools instead of a top-level menu.
    if( UseModernMenuLayout() )
        return;

    COMMON_CONTROL* tool = m_toolManager->GetTool<COMMON_CONTROL>();

    prefsMenu->Add( ACTIONS::configurePaths );
    prefsMenu->Add( ACTIONS::showFootprintLibTable );
    prefsMenu->Add( CVPCB_ACTIONS::showEquFileTable );
    prefsMenu->Add( ACTIONS::openPreferences );

    prefsMenu->AppendSeparator();
    AddMenuLanguageList( prefsMenu, tool );
}
