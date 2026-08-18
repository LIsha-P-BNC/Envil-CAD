/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
* Copyright (C) 2023 CERN
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

#include <sim/toolbars_simulator_frame.h>

#include <sim/simulator_frame.h>
#include <tool/action_menu.h>
#include <tool/action_toolbar.h>
#include <tool/common_control.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <widgets/wx_menubar.h>


std::optional<TOOLBAR_CONFIGURATION> SIMULATOR_TOOLBAR_SETTINGS::DefaultToolbarConfig( TOOLBAR_LOC aToolbar )
{
    TOOLBAR_CONFIGURATION config;

    // clang-format off
    switch( aToolbar )
    {
    case TOOLBAR_LOC::LEFT:
    case TOOLBAR_LOC::RIGHT:
    case TOOLBAR_LOC::TOP_AUX:
    case TOOLBAR_LOC::ACTIVE_BAR:   // Active Bar is PCB-editor-only; no bar here.
        return std::nullopt;

    case TOOLBAR_LOC::TOP_MAIN:
        config.AppendAction( SCH_ACTIONS::openWorkbook )
              .AppendAction( SCH_ACTIONS::saveWorkbook );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::newAnalysisTab )
              .AppendAction( SCH_ACTIONS::simAnalysisProperties );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::runSimulation )
              .AppendAction( SCH_ACTIONS::stopSimulation );

        config.AppendSeparator()
              .AppendAction( ACTIONS::zoomInCenter )
              .AppendAction( ACTIONS::zoomOutCenter )
              .AppendAction( ACTIONS::zoomInHorizontally )
              .AppendAction( ACTIONS::zoomOutHorizontally )
              .AppendAction( ACTIONS::zoomInVertically )
              .AppendAction( ACTIONS::zoomOutVertically )
              .AppendAction( ACTIONS::zoomFitScreen );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::simProbe )
              .AppendAction( SCH_ACTIONS::simTune );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::editUserDefinedSignals )
              .AppendAction( SCH_ACTIONS::showNetlist );
        break;
    }

    // clang-format on
    return config;
}


void SIMULATOR_FRAME::doReCreateMenuBar()
{
    // Anvil Next: build the shared common menu bar instead of the legacy one when enabled.
    if( UseUnifiedMenuBar() )
    {
        buildCommonMenuBar();
        return;
    }

    COMMON_CONTROL* tool = m_toolManager->GetTool<COMMON_CONTROL>();
    EDA_BASE_FRAME* base_frame = dynamic_cast<EDA_BASE_FRAME*>( this );

    // base_frame == nullptr should not happen, but it makes Coverity happy
    wxCHECK( base_frame, /* void */ );

    // wxWidgets handles the OSX Application menu behind the scenes, but that means
    // we always have to start from scratch with a new wxMenuBar.
    wxMenuBar*  oldMenuBar = base_frame->GetMenuBar();
    WX_MENUBAR* menuBar    = new WX_MENUBAR();

    //-- File menu -----------------------------------------------------------
    //
    ACTION_MENU* fileMenu = new ACTION_MENU( false, tool );

    fileMenu->Add( SCH_ACTIONS::newAnalysisTab );

    fileMenu->AppendSeparator();
    fileMenu->Add( SCH_ACTIONS::openWorkbook );
    fileMenu->Add( SCH_ACTIONS::saveWorkbook );
    fileMenu->Add( SCH_ACTIONS::saveWorkbookAs );

    fileMenu->AppendSeparator();
    fileMenu->Add( SCH_ACTIONS::exportPlotAsPNG );
    fileMenu->Add( SCH_ACTIONS::exportPlotAsCSV );
    fileMenu->AppendSeparator();
    fileMenu->Add( SCH_ACTIONS::exportPlotToClipboard );
    fileMenu->Add( SCH_ACTIONS::exportPlotToSchematic );

    fileMenu->AppendSeparator();
    fileMenu->AddClose( _( "Simulator" ) );


    //-- View menu -----------------------------------------------------------
    //
    ACTION_MENU* viewMenu = new ACTION_MENU( false, tool );
    // clang-format off
    viewMenu->Add( SCH_ACTIONS::toggleSimSidePanel, ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleSimConsole,   ACTION_MENU::CHECK );
    // clang-format on

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomUndo );
    viewMenu->Add( ACTIONS::zoomRedo );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomInCenter );
    viewMenu->Add( ACTIONS::zoomOutCenter );
    viewMenu->Add( ACTIONS::zoomInHorizontally );
    viewMenu->Add( ACTIONS::zoomOutHorizontally );
    viewMenu->Add( ACTIONS::zoomInVertically );
    viewMenu->Add( ACTIONS::zoomOutVertically );
    viewMenu->Add( ACTIONS::zoomFitScreen );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::toggleGrid,               ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleLegend,          ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleDottedSecondary, ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleDarkModePlots,   ACTION_MENU::CHECK );


    //-- Simulation menu -----------------------------------------------------------
    //
    ACTION_MENU* simulationMenu = new ACTION_MENU( false, tool );

    simulationMenu->Add( SCH_ACTIONS::newAnalysisTab );
    simulationMenu->Add( SCH_ACTIONS::simAnalysisProperties );
    simulationMenu->Add( SCH_ACTIONS::runSimulation );

    simulationMenu->AppendSeparator();
    simulationMenu->Add( SCH_ACTIONS::simProbe );
    simulationMenu->Add( SCH_ACTIONS::simTune );

    simulationMenu->AppendSeparator();
    simulationMenu->Add( SCH_ACTIONS::editUserDefinedSignals );
    simulationMenu->Add( SCH_ACTIONS::showNetlist );


    //-- Preferences menu -----------------------------------------------
    //
    ACTION_MENU* prefsMenu = new ACTION_MENU( false, tool );

    //prefsMenu->Add( ACTIONS::configurePaths );
    //prefsMenu->Add( SCH_ACTIONS::showSimLibTable );
    prefsMenu->Add( ACTIONS::openPreferences );

    prefsMenu->AppendSeparator();
    AddMenuLanguageList( prefsMenu, tool );


    //-- Menubar -------------------------------------------------------------
    //
    menuBar->Append( fileMenu, _( "&File" ) );
    menuBar->Append( viewMenu, _( "&View" ) );
    menuBar->Append( simulationMenu, _( "&Simulation" ) );
    menuBar->Append( prefsMenu, _( "&Preferences" ) );
    base_frame->AddStandardHelpMenu( menuBar );

    base_frame->SetMenuBar( menuBar );
    delete oldMenuBar;
}


//================================ Anvil Next unified menu bar ================================
// The following hooks reproduce the menus built above, but populate a menu supplied by the
// shared EDA_BASE_FRAME::buildCommonMenuBar() orchestrator.  The unified assembler has no
// "Simulation" top-level slot, so its items live under Tools there (the legacy path above
// keeps the classic Simulation menu — nothing is lost either way).

TOOL_INTERACTIVE* SIMULATOR_FRAME::getCurrentMenuTool()
{
    return m_toolManager->GetTool<COMMON_CONTROL>();
}


void SIMULATOR_FRAME::buildFileMenu( ACTION_MENU* fileMenu )
{
    fileMenu->Add( SCH_ACTIONS::newAnalysisTab );

    fileMenu->AppendSeparator();
    fileMenu->Add( SCH_ACTIONS::openWorkbook );
    fileMenu->Add( SCH_ACTIONS::saveWorkbook );
    fileMenu->Add( SCH_ACTIONS::saveWorkbookAs );

    fileMenu->AppendSeparator();
    fileMenu->Add( SCH_ACTIONS::exportPlotAsPNG );
    fileMenu->Add( SCH_ACTIONS::exportPlotAsCSV );

    fileMenu->AppendSeparator();
    fileMenu->Add( SCH_ACTIONS::exportPlotToClipboard );
    fileMenu->Add( SCH_ACTIONS::exportPlotToSchematic );

    fileMenu->AppendSeparator();
    fileMenu->AddClose( _( "Simulator" ) );
}


void SIMULATOR_FRAME::buildViewMenu( ACTION_MENU* viewMenu )
{
    viewMenu->Add( SCH_ACTIONS::toggleSimSidePanel, ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleSimConsole,   ACTION_MENU::CHECK );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomUndo );
    viewMenu->Add( ACTIONS::zoomRedo );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomInCenter );
    viewMenu->Add( ACTIONS::zoomOutCenter );
    viewMenu->Add( ACTIONS::zoomInHorizontally );
    viewMenu->Add( ACTIONS::zoomOutHorizontally );
    viewMenu->Add( ACTIONS::zoomInVertically );
    viewMenu->Add( ACTIONS::zoomOutVertically );
    viewMenu->Add( ACTIONS::zoomFitScreen );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::toggleGrid,                ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleLegend,          ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleDottedSecondary, ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleDarkModePlots,   ACTION_MENU::CHECK );
}


void SIMULATOR_FRAME::buildToolsMenu( ACTION_MENU* toolsMenu )
{
    COMMON_CONTROL* tool = m_toolManager->GetTool<COMMON_CONTROL>();

    toolsMenu->Add( SCH_ACTIONS::newAnalysisTab );
    toolsMenu->Add( SCH_ACTIONS::simAnalysisProperties );
    toolsMenu->Add( SCH_ACTIONS::runSimulation );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::simProbe );
    toolsMenu->Add( SCH_ACTIONS::simTune );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::editUserDefinedSignals );
    toolsMenu->Add( SCH_ACTIONS::showNetlist );

    if( UseModernMenuLayout() )
    {
        toolsMenu->AppendSeparator();
        toolsMenu->Add( ACTIONS::openPreferences );

        toolsMenu->AppendSeparator();
        AddMenuLanguageList( toolsMenu, tool );
    }
}


void SIMULATOR_FRAME::buildPreferencesMenu( ACTION_MENU* prefsMenu )
{
    // Modern layout: these items live in the tail of Tools instead of a top-level menu.
    if( UseModernMenuLayout() )
        return;

    COMMON_CONTROL* tool = m_toolManager->GetTool<COMMON_CONTROL>();

    prefsMenu->Add( ACTIONS::openPreferences );

    prefsMenu->AppendSeparator();
    AddMenuLanguageList( prefsMenu, tool );
}
