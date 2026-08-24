/*
 * This program source code file is part of Anvil, a free EDA CAD application.
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
 * Anvil Next: the shared "common" top menu-bar assembler.
 *
 * Instead of every frame building its own menu bar from scratch in doReCreateMenuBar(), the
 * unified path builds a single canonical menu bar and lets each frame populate only the menus
 * it owns through the virtual build*Menu() hooks declared on EDA_BASE_FRAME.  A menu that a
 * frame does not fill is skipped, so the same builder yields the schematic editor's full menu
 * set, the PCB editor's Route menu, the Gerber viewer's no-Edit/no-Place bar, and the
 * calculator's File-only bar.
 *
 * Two top-level groupings are supported:
 *  - classic (default): File, Edit, View, Place, Route, Inspect, Tools, Preferences, Help
 *  - modern (ModernMenuLayout): the Altium-style set File, Edit, View, Project, Place, Design,
 *    Route, Reports, Tools, Window, Help.  The extra menus come from the buildProjectMenu() /
 *    buildDesignMenu() / buildReportsMenu() hooks, Window is assembled centrally below, and a
 *    modernised frame folds its Preferences items into the tail of Tools.  Inspect and
 *    Preferences remain as safety nets in the modern layout: a frame that has not adopted the
 *    modern hooks keeps its full classic menus, so no command is ever lost.
 *
 * This file is additive: it is reached only when EDA_BASE_FRAME::UseUnifiedMenuBar() is true,
 * and the legacy per-frame doReCreateMenuBar() bodies remain the default code path.
 */

#include <advanced_config.h>
#include <eda_base_frame.h>
#include <tool/action_menu.h>
#include <tool/tool_interactive.h>
#include <widgets/wx_menubar.h>

#include <map>
#include <vector>

#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/window.h>


static std::function<void( EDA_BASE_FRAME* )> s_windowMenuActivator;

// AnvilCAD MCP menu hooks; registered by the shell (which owns the MCP socket server).
static EDA_BASE_FRAME::MCP_MENU_CONTROLLER s_mcpMenuController;


bool EDA_BASE_FRAME::UseUnifiedMenuBar()
{
    const ADVANCED_CFG& cfg = ADVANCED_CFG::GetCfg();

    return cfg.m_UnifiedMenuBar || cfg.m_ModernMenuLayout;
}


bool EDA_BASE_FRAME::UseModernMenuLayout()
{
    return ADVANCED_CFG::GetCfg().m_ModernMenuLayout;
}


void EDA_BASE_FRAME::SetWindowMenuActivator( std::function<void( EDA_BASE_FRAME* )> aActivator )
{
    s_windowMenuActivator = std::move( aActivator );
}


void EDA_BASE_FRAME::SetMcpMenuController( MCP_MENU_CONTROLLER aController )
{
    s_mcpMenuController = std::move( aController );
}


void EDA_BASE_FRAME::ActivateWindowMenuTarget( EDA_BASE_FRAME* aFrame )
{
    if( !aFrame )
        return;

    // The single-window shell installs an activator that selects the frame's editor tab.
    if( s_windowMenuActivator )
    {
        s_windowMenuActivator( aFrame );
        return;
    }

    // Default multi-window behaviour: bring the frame forward.
    if( aFrame->IsIconized() )
        aFrame->Iconize( false );

    aFrame->Show( true );
    aFrame->Raise();
}


namespace
{
/**
 * The modern layout's Window menu: lists every open Anvil frame and activates the chosen one
 * (raise in multi-window mode, tab selection in the single-window shell).  The item list is
 * rebuilt each time the menu is opened, so it always reflects the current window set.
 */
class WINDOW_MENU : public ACTION_MENU
{
public:
    WINDOW_MENU( TOOL_INTERACTIVE* aTool, EDA_BASE_FRAME* aOwner ) :
            ACTION_MENU( false, aTool ),
            m_owner( aOwner )
    {
        SetTitle( _( "Window" ) );

        // Item commands are handled by the menu itself; menubar open events arrive at the
        // owning frame, so listen there to rebuild just before the menu is shown.
        Bind( wxEVT_MENU, &WINDOW_MENU::onWindowItem, this );
        m_owner->Bind( wxEVT_MENU_OPEN, &WINDOW_MENU::onMenuOpen, this );

        rebuild();
    }

    ~WINDOW_MENU() override
    {
        if( m_owner )
            m_owner->Unbind( wxEVT_MENU_OPEN, &WINDOW_MENU::onMenuOpen, this );
    }

protected:
    void update() override
    {
        rebuild();
    }

private:
    void onMenuOpen( wxMenuEvent& aEvent )
    {
        if( aEvent.GetMenu() == this )
            rebuild();

        aEvent.Skip();
    }

    void rebuild()
    {
        for( int i = (int) GetMenuItemCount() - 1; i >= 0; --i )
            Destroy( FindItemByPosition( i ) );

        m_targets.clear();

        // wxTopLevelWindows is in creation order, which puts the project manager (when it
        // exists) first and the editors after it.
        std::vector<EDA_BASE_FRAME*> frames;

        for( wxWindow* window : wxTopLevelWindows )
        {
            EDA_BASE_FRAME* frame = dynamic_cast<EDA_BASE_FRAME*>( window );

            if( frame && frame->IsShown() && !frame->GetTitle().IsEmpty() )
                frames.push_back( frame );
        }

        int index = 1;

        for( EDA_BASE_FRAME* frame : frames )
        {
            wxString label = frame->GetTitle();

            if( index <= 9 )
                label = wxString::Format( wxT( "&%d  %s" ), index, label );

            wxMenuItem* item = AppendCheckItem( wxID_ANY, label );

            item->Check( frame == m_owner );
            m_targets[item->GetId()] = frame;
            index++;
        }

        if( m_targets.empty() )
            Append( wxID_ANY, _( "No open windows" ) )->Enable( false );
    }

    void onWindowItem( wxCommandEvent& aEvent )
    {
        auto it = m_targets.find( aEvent.GetId() );

        if( it != m_targets.end() )
            EDA_BASE_FRAME::ActivateWindowMenuTarget( it->second );
        else
            aEvent.Skip();
    }

    EDA_BASE_FRAME*                m_owner;
    std::map<int, EDA_BASE_FRAME*> m_targets;
};


/**
 * The "AnvilCAD MCP" menu: Start / Stop the process-wide MCP socket through the controller
 * hooks the shell registered.  Item enable-state is refreshed each time the menu is opened,
 * so it always reflects whether the server is currently listening.
 */
class MCP_MENU : public ACTION_MENU
{
public:
    MCP_MENU( TOOL_INTERACTIVE* aTool, EDA_BASE_FRAME* aOwner ) :
            ACTION_MENU( false, aTool ),
            m_owner( aOwner )
    {
        SetTitle( _( "AnvilCAD MCP" ) );

        m_startItem = Append( wxID_ANY, _( "Start MCP Connection" ) );
        m_stopItem  = Append( wxID_ANY, _( "Stop MCP Connection" ) );

        // Same routing as WINDOW_MENU: item commands are handled by the menu itself, while
        // menubar open events arrive at the owning frame.
        Bind( wxEVT_MENU, &MCP_MENU::onCommand, this );
        m_owner->Bind( wxEVT_MENU_OPEN, &MCP_MENU::onMenuOpen, this );

        refresh();
    }

    ~MCP_MENU() override
    {
        if( m_owner )
            m_owner->Unbind( wxEVT_MENU_OPEN, &MCP_MENU::onMenuOpen, this );
    }

protected:
    void update() override
    {
        refresh();
    }

private:
    void onMenuOpen( wxMenuEvent& aEvent )
    {
        if( aEvent.GetMenu() == this )
            refresh();

        aEvent.Skip();
    }

    void refresh()
    {
        bool running = s_mcpMenuController.isRunning && s_mcpMenuController.isRunning();

        m_startItem->Enable( !running );
        m_stopItem->Enable( running );
    }

    void onCommand( wxCommandEvent& aEvent )
    {
        if( aEvent.GetId() == m_startItem->GetId() )
        {
            if( s_mcpMenuController.start && !s_mcpMenuController.start() )
            {
                int port = s_mcpMenuController.port ? s_mcpMenuController.port() : 0;

                wxMessageBox( wxString::Format( _( "Could not start the MCP connection: "
                                                   "port %d is already in use." ),
                                                port ),
                              _( "AnvilCAD MCP" ), wxICON_ERROR | wxOK, m_owner );
            }

            refresh();
        }
        else if( aEvent.GetId() == m_stopItem->GetId() )
        {
            if( s_mcpMenuController.stop )
                s_mcpMenuController.stop();

            refresh();
        }
        else
        {
            aEvent.Skip();
        }
    }

    EDA_BASE_FRAME* m_owner;
    wxMenuItem*     m_startItem;
    wxMenuItem*     m_stopItem;
};

} // namespace


void EDA_BASE_FRAME::buildCommonMenuBar()
{
    // Build this frame's menu from its own hooks (the original behaviour).
    buildCommonMenuBarFrom( this );
}


void EDA_BASE_FRAME::buildCommonMenuBarFrom( EDA_BASE_FRAME* aSource,
                                             EDA_BASE_FRAME* aCommonSource )
{
    wxCHECK_RET( aSource, wxT( "buildCommonMenuBarFrom() requires a non-null source frame" ) );

    if( !aCommonSource )
        aCommonSource = aSource;

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

                // Compose the shell's common commands with the active editor's commands only
                // when they come from different frames.  With no editor open, the shell must
                // show its normal menu exactly once.
                if( aSource != aCommonSource )
                {
                    TOOL_INTERACTIVE* commonTool = aCommonSource->getCurrentMenuTool();
                    ACTION_MENU* commonMenu = new ACTION_MENU( false, commonTool );
                    ( aCommonSource->*aBuilder )( commonMenu );
                    menu->AppendFrom( *commonMenu );
                    delete commonMenu;
                }

                ( aSource->*aBuilder )( menu );

                // A frame opts into a top-level menu simply by adding items in its hook; an empty
                // menu means "this frame has no such menu" and is dropped.
                if( menu->GetMenuItemCount() > 0 )
                    menuBar->Append( menu, aTitle );
                else
                    delete menu;
            };

    bool modern = UseModernMenuLayout();

    appendIfNonEmpty( _( "&File" ),        &EDA_BASE_FRAME::buildFileMenu );
    appendIfNonEmpty( _( "&Edit" ),        &EDA_BASE_FRAME::buildEditMenu );
    appendIfNonEmpty( _( "&View" ),        &EDA_BASE_FRAME::buildViewMenu );

    if( modern )
        appendIfNonEmpty( _( "P&roject" ), &EDA_BASE_FRAME::buildProjectMenu );

    appendIfNonEmpty( _( "&Place" ),       &EDA_BASE_FRAME::buildPlaceMenu );

    if( modern )
        appendIfNonEmpty( _( "&Design" ),  &EDA_BASE_FRAME::buildDesignMenu );

    appendIfNonEmpty( _( "Ro&ute" ),       &EDA_BASE_FRAME::buildRouteMenu );

    // Kept in both layouts as a safety net: a modernised frame leaves Inspect empty in the
    // modern layout (its items move to Tools / Reports), while a frame that has not adopted
    // the modern hooks keeps its full classic Inspect menu — nothing is lost either way.
    appendIfNonEmpty( _( "&Inspect" ),     &EDA_BASE_FRAME::buildInspectMenu );

    if( modern )
        appendIfNonEmpty( _( "Re&ports" ), &EDA_BASE_FRAME::buildReportsMenu );

    appendIfNonEmpty( _( "&Tools" ),       &EDA_BASE_FRAME::buildToolsMenu );

    // Same safety net as Inspect: modernised frames fold these items into the tail of Tools
    // when the modern layout is active and leave this hook empty.
    appendIfNonEmpty( _( "P&references" ), &EDA_BASE_FRAME::buildPreferencesMenu );

    if( modern )
        menuBar->Append( new WINDOW_MENU( tool, this ), _( "&Window" ) );

    // AnvilCAD MCP is process-wide, so it appears in every tool's menu bar — but only when
    // the shell has registered a controller (standalone editors have no MCP server).
    if( s_mcpMenuController.start )
        menuBar->Append( new MCP_MENU( tool, this ), _( "AnvilCAD MCP" ) );

    // Help is identical for every frame and already shared.
    aSource->AddStandardHelpMenu( menuBar );

    SetMenuBar( menuBar );
    delete oldMenuBar;
}
