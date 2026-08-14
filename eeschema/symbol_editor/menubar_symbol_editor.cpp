/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2007 Jean-Pierre Charras, jaen-pierre.charras@gipsa-lab.inpg.com
 * Copyright (C) 2009 Wayne Stambaugh <stambaughw@gmail.com>
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
#include <tool/action_menu.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <tools/sch_selection_tool.h>
#include <symbol_library_manager.h>
#include "symbol_edit_frame.h"
#include <widgets/wx_menubar.h>
#include <advanced_config.h>


void SYMBOL_EDIT_FRAME::doReCreateMenuBar()
{
    // KiCad Next: build the shared common menu bar instead of the legacy one when enabled.
    if( UseUnifiedMenuBar() )
    {
        buildCommonMenuBar();
        return;
    }

    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();
    // wxWidgets handles the Mac Application menu behind the scenes, but that means
    // we always have to start from scratch with a new wxMenuBar.
    wxMenuBar*  oldMenuBar = GetMenuBar();
    WX_MENUBAR* menuBar    = new WX_MENUBAR();

    //-- File menu -----------------------------------------------
    //
    ACTION_MENU* fileMenu = new ACTION_MENU( false, selTool );

    fileMenu->Add( ACTIONS::newLibrary );
    fileMenu->Add( ACTIONS::addLibrary );
    fileMenu->Add( SCH_ACTIONS::saveLibraryAs );
    fileMenu->Add( SCH_ACTIONS::newSymbol );
    fileMenu->Add( SCH_ACTIONS::editLibSymbolWithLibEdit );

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::save );
    fileMenu->Add( SCH_ACTIONS::saveSymbolAs );
    fileMenu->Add( SCH_ACTIONS::saveSymbolCopyAs );

    if( !IsSymbolFromSchematic() )
        fileMenu->Add( ACTIONS::saveAll );

    fileMenu->Add( ACTIONS::revert );

    fileMenu->AppendSeparator();

    ACTION_MENU* submenuImport = new ACTION_MENU( false, selTool );
    submenuImport->SetTitle( _( "Import" ) );
    submenuImport->SetIcon( BITMAPS::import );

    submenuImport->Add( SCH_ACTIONS::importSymbol,   ACTION_MENU::NORMAL, _( "Symbol..." ) );
    submenuImport->Add( SCH_ACTIONS::importGraphics, ACTION_MENU::NORMAL, _( "Graphics..." ) );

    fileMenu->Add( submenuImport );

    // Export submenu
    ACTION_MENU* submenuExport = new ACTION_MENU( false, selTool );
    submenuExport->SetTitle( _( "Export" ) );
    submenuExport->SetIcon( BITMAPS::export_file );
    submenuExport->Add( SCH_ACTIONS::exportSymbol,      ACTION_MENU::NORMAL, _( "Symbol..." ) );
    submenuExport->Add( SCH_ACTIONS::exportSymbolView,  ACTION_MENU::NORMAL, _( "View as PNG..." ) );
    submenuExport->Add( SCH_ACTIONS::exportSymbolAsSVG, ACTION_MENU::NORMAL, _( "Symbol as SVG..." ) );
    fileMenu->Add( submenuExport );

    fileMenu->AppendSeparator();
    fileMenu->Add( SCH_ACTIONS::symbolProperties );

    fileMenu->AppendSeparator();
    fileMenu->AddClose( _( "Library Editor" ) );


    //-- Edit menu -----------------------------------------------
    //
    ACTION_MENU* editMenu = new ACTION_MENU( false, selTool );

    editMenu->Add( ACTIONS::undo );
    editMenu->Add( ACTIONS::redo );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut );
    editMenu->Add( ACTIONS::copy );
    editMenu->Add( ACTIONS::copyAsText );
    editMenu->Add( ACTIONS::paste );
    editMenu->Add( ACTIONS::doDelete );
    editMenu->Add( ACTIONS::duplicate );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::selectAll );
    editMenu->Add( ACTIONS::unselectAll );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::find );
    editMenu->Add( ACTIONS::findAndReplace );

    editMenu->AppendSeparator();
    editMenu->Add( SCH_ACTIONS::pinTable );
    editMenu->Add( SCH_ACTIONS::updateSymbolFields );


    //-- View menu -----------------------------------------------
    //
    ACTION_MENU* viewMenu = new ACTION_MENU( false, selTool );

    ACTION_MENU* showHidePanels = new ACTION_MENU( false, selTool );
    showHidePanels->SetTitle( _( "Panels" ) );
    buildPanelsMenu( showHidePanels );
    viewMenu->Add( showHidePanels );
    viewMenu->AppendSeparator();

    viewMenu->Add( ACTIONS::showSymbolBrowser );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomInCenter );
    viewMenu->Add( ACTIONS::zoomOutCenter );
    viewMenu->Add( ACTIONS::zoomFitScreen );
    viewMenu->Add( ACTIONS::zoomTool );
    viewMenu->Add( ACTIONS::zoomRedraw );

    // Modern toolbar preset: the left toolbar is gone, so its display toggles surface here.
    if( ADVANCED_CFG::GetCfg().m_ModernToolbarLayout )
    {
        viewMenu->AppendSeparator();
        viewMenu->Add( ACTIONS::toggleGrid,          ACTION_MENU::CHECK );
        viewMenu->Add( ACTIONS::toggleGridOverrides, ACTION_MENU::CHECK );

        ACTION_MENU* unitsSubMenu = new ACTION_MENU( false, selTool );
        unitsSubMenu->SetTitle( _( "&Units" ) );
        unitsSubMenu->Add( ACTIONS::millimetersUnits, ACTION_MENU::CHECK );
        unitsSubMenu->Add( ACTIONS::inchesUnits,      ACTION_MENU::CHECK );
        unitsSubMenu->Add( ACTIONS::milsUnits,        ACTION_MENU::CHECK );
        viewMenu->Add( unitsSubMenu );

        ACTION_MENU* crosshairSubMenu = new ACTION_MENU( false, selTool );
        crosshairSubMenu->SetTitle( _( "&Crosshair Mode" ) );
        crosshairSubMenu->Add( ACTIONS::cursorSmallCrosshairs, ACTION_MENU::CHECK );
        crosshairSubMenu->Add( ACTIONS::cursorFullCrosshairs,  ACTION_MENU::CHECK );
        crosshairSubMenu->Add( ACTIONS::cursor45Crosshairs,    ACTION_MENU::CHECK );
        viewMenu->Add( crosshairSubMenu );

        viewMenu->Add( SCH_ACTIONS::showElectricalTypes, ACTION_MENU::CHECK );
    }

    viewMenu->AppendSeparator();
    viewMenu->Add( SCH_ACTIONS::showHiddenPins,    ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::showHiddenFields,  ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::togglePinAltIcons, ACTION_MENU::CHECK );


    //-- Place menu -----------------------------------------------
    //
    ACTION_MENU* placeMenu = new ACTION_MENU( false, selTool );

    placeMenu->Add( SCH_ACTIONS::placeSymbolPin );
    placeMenu->Add( SCH_ACTIONS::placeSymbolText );
    placeMenu->Add( SCH_ACTIONS::drawSymbolTextBox );
    placeMenu->Add( SCH_ACTIONS::drawRectangle );
    placeMenu->Add( SCH_ACTIONS::drawCircle );
    placeMenu->Add( SCH_ACTIONS::drawArc );
    placeMenu->Add( SCH_ACTIONS::drawBezier );
    placeMenu->Add( SCH_ACTIONS::drawSymbolLines );
    placeMenu->Add( SCH_ACTIONS::drawSymbolPolygon );


    //-- Inspect menu -----------------------------------------------
    //
    ACTION_MENU* inspectMenu = new ACTION_MENU( false, selTool );

    inspectMenu->Add( ACTIONS::showDatasheet );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( SCH_ACTIONS::checkSymbol );


    //-- Preferences menu -----------------------------------------------
    //
    ACTION_MENU* prefsMenu = new ACTION_MENU( false, selTool );

    prefsMenu->Add( ACTIONS::configurePaths );
    prefsMenu->Add( ACTIONS::showSymbolLibTable );
    prefsMenu->Add( ACTIONS::openPreferences );

    prefsMenu->AppendSeparator();
    AddMenuLanguageList( prefsMenu, selTool );


    //-- Menubar -------------------------------------------------------------
    //
    menuBar->Append( fileMenu,    _( "&File" ) );
    menuBar->Append( editMenu,    _( "&Edit" ) );
    menuBar->Append( viewMenu,    _( "&View" ) );
    menuBar->Append( placeMenu,   _( "&Place" ) );
    menuBar->Append( inspectMenu, _( "&Inspect" ) );
    menuBar->Append( prefsMenu,   _( "P&references" ) );
    AddStandardHelpMenu( menuBar );

    SetMenuBar( menuBar );
    delete oldMenuBar;
}


//================================ KiCad Next unified menu bar ================================
// The following hooks reproduce the menus built above, but populate a menu supplied by the
// shared EDA_BASE_FRAME::buildCommonMenuBar() orchestrator.  They are used only when the
// m_UnifiedMenuBar advanced flag is set; otherwise the legacy doReCreateMenuBar() above runs.

TOOL_INTERACTIVE* SYMBOL_EDIT_FRAME::getCurrentMenuTool()
{
    return m_toolManager->GetTool<SCH_SELECTION_TOOL>();
}


void SYMBOL_EDIT_FRAME::buildPanelsMenu( ACTION_MENU* aMenu )
{
    aMenu->Add( ACTIONS::showProperties,  ACTION_MENU::CHECK );
    aMenu->Add( ACTIONS::showLibraryTree, ACTION_MENU::CHECK );
}


void SYMBOL_EDIT_FRAME::buildFileMenu( ACTION_MENU* fileMenu )
{
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();

    fileMenu->Add( ACTIONS::newLibrary );
    fileMenu->Add( ACTIONS::addLibrary );
    fileMenu->Add( SCH_ACTIONS::saveLibraryAs );
    fileMenu->Add( SCH_ACTIONS::newSymbol );
    fileMenu->Add( SCH_ACTIONS::editLibSymbolWithLibEdit );

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::save );
    fileMenu->Add( SCH_ACTIONS::saveSymbolAs );
    fileMenu->Add( SCH_ACTIONS::saveSymbolCopyAs );

    if( !IsSymbolFromSchematic() )
        fileMenu->Add( ACTIONS::saveAll );

    fileMenu->Add( ACTIONS::revert );

    fileMenu->AppendSeparator();

    ACTION_MENU* submenuImport = new ACTION_MENU( false, selTool );
    submenuImport->SetTitle( _( "Import" ) );
    submenuImport->SetIcon( BITMAPS::import );

    submenuImport->Add( SCH_ACTIONS::importSymbol,   ACTION_MENU::NORMAL, _( "Symbol..." ) );
    submenuImport->Add( SCH_ACTIONS::importGraphics, ACTION_MENU::NORMAL, _( "Graphics..." ) );

    fileMenu->Add( submenuImport );

    // Export submenu
    ACTION_MENU* submenuExport = new ACTION_MENU( false, selTool );
    submenuExport->SetTitle( _( "Export" ) );
    submenuExport->SetIcon( BITMAPS::export_file );
    submenuExport->Add( SCH_ACTIONS::exportSymbol,      ACTION_MENU::NORMAL, _( "Symbol..." ) );
    submenuExport->Add( SCH_ACTIONS::exportSymbolView,  ACTION_MENU::NORMAL, _( "View as PNG..." ) );
    submenuExport->Add( SCH_ACTIONS::exportSymbolAsSVG, ACTION_MENU::NORMAL, _( "Symbol as SVG..." ) );
    fileMenu->Add( submenuExport );

    fileMenu->AppendSeparator();
    fileMenu->Add( SCH_ACTIONS::symbolProperties );

    fileMenu->AppendSeparator();
    fileMenu->AddClose( _( "Library Editor" ) );
}


void SYMBOL_EDIT_FRAME::buildEditMenu( ACTION_MENU* editMenu )
{
    editMenu->Add( ACTIONS::undo );
    editMenu->Add( ACTIONS::redo );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut );
    editMenu->Add( ACTIONS::copy );
    editMenu->Add( ACTIONS::copyAsText );
    editMenu->Add( ACTIONS::paste );
    editMenu->Add( ACTIONS::doDelete );
    editMenu->Add( ACTIONS::duplicate );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::selectAll );
    editMenu->Add( ACTIONS::unselectAll );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::find );
    editMenu->Add( ACTIONS::findAndReplace );

    editMenu->AppendSeparator();
    editMenu->Add( SCH_ACTIONS::pinTable );
    editMenu->Add( SCH_ACTIONS::updateSymbolFields );
}


void SYMBOL_EDIT_FRAME::buildViewMenu( ACTION_MENU* viewMenu )
{
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();

    ACTION_MENU* showHidePanels = new ACTION_MENU( false, selTool );
    showHidePanels->SetTitle( _( "Panels" ) );
    buildPanelsMenu( showHidePanels );
    viewMenu->Add( showHidePanels );
    viewMenu->AppendSeparator();

    viewMenu->Add( ACTIONS::showSymbolBrowser );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomInCenter );
    viewMenu->Add( ACTIONS::zoomOutCenter );
    viewMenu->Add( ACTIONS::zoomFitScreen );
    viewMenu->Add( ACTIONS::zoomTool );
    viewMenu->Add( ACTIONS::zoomRedraw );

    // Modern toolbar preset: the left toolbar is gone, so its display toggles surface here.
    if( ADVANCED_CFG::GetCfg().m_ModernToolbarLayout )
    {
        viewMenu->AppendSeparator();
        viewMenu->Add( ACTIONS::toggleGrid,          ACTION_MENU::CHECK );
        viewMenu->Add( ACTIONS::toggleGridOverrides, ACTION_MENU::CHECK );

        ACTION_MENU* unitsSubMenu = new ACTION_MENU( false, selTool );
        unitsSubMenu->SetTitle( _( "&Units" ) );
        unitsSubMenu->Add( ACTIONS::millimetersUnits, ACTION_MENU::CHECK );
        unitsSubMenu->Add( ACTIONS::inchesUnits,      ACTION_MENU::CHECK );
        unitsSubMenu->Add( ACTIONS::milsUnits,        ACTION_MENU::CHECK );
        viewMenu->Add( unitsSubMenu );

        ACTION_MENU* crosshairSubMenu = new ACTION_MENU( false, selTool );
        crosshairSubMenu->SetTitle( _( "&Crosshair Mode" ) );
        crosshairSubMenu->Add( ACTIONS::cursorSmallCrosshairs, ACTION_MENU::CHECK );
        crosshairSubMenu->Add( ACTIONS::cursorFullCrosshairs,  ACTION_MENU::CHECK );
        crosshairSubMenu->Add( ACTIONS::cursor45Crosshairs,    ACTION_MENU::CHECK );
        viewMenu->Add( crosshairSubMenu );

        viewMenu->Add( SCH_ACTIONS::showElectricalTypes, ACTION_MENU::CHECK );
    }

    viewMenu->AppendSeparator();
    viewMenu->Add( SCH_ACTIONS::showHiddenPins,    ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::showHiddenFields,  ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::togglePinAltIcons, ACTION_MENU::CHECK );
}


void SYMBOL_EDIT_FRAME::buildPlaceMenu( ACTION_MENU* placeMenu )
{
    placeMenu->Add( SCH_ACTIONS::placeSymbolPin );
    placeMenu->Add( SCH_ACTIONS::placeSymbolText );
    placeMenu->Add( SCH_ACTIONS::drawSymbolTextBox );
    placeMenu->Add( SCH_ACTIONS::drawRectangle );
    placeMenu->Add( SCH_ACTIONS::drawCircle );
    placeMenu->Add( SCH_ACTIONS::drawArc );
    placeMenu->Add( SCH_ACTIONS::drawBezier );
    placeMenu->Add( SCH_ACTIONS::drawSymbolLines );
    placeMenu->Add( SCH_ACTIONS::drawSymbolPolygon );
}


void SYMBOL_EDIT_FRAME::buildInspectMenu( ACTION_MENU* inspectMenu )
{
    // Modern layout: the symbol checker moves to Tools and the datasheet to Reports, so the
    // classic Inspect menu is dropped entirely.
    if( UseModernMenuLayout() )
        return;

    inspectMenu->Add( ACTIONS::showDatasheet );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( SCH_ACTIONS::checkSymbol );
}


void SYMBOL_EDIT_FRAME::buildToolsMenu( ACTION_MENU* toolsMenu )
{
    // The classic symbol editor has no Tools menu; the modern layout gains one for the
    // checker, with the Preferences items folded into the tail until the title-bar gear
    // hosts them.
    if( !UseModernMenuLayout() )
        return;

    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();

    toolsMenu->Add( SCH_ACTIONS::checkSymbol );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::configurePaths );
    toolsMenu->Add( ACTIONS::showSymbolLibTable );
    toolsMenu->Add( ACTIONS::openPreferences );

    toolsMenu->AppendSeparator();
    AddMenuLanguageList( toolsMenu, selTool );
}


void SYMBOL_EDIT_FRAME::buildReportsMenu( ACTION_MENU* reportsMenu )
{
    reportsMenu->Add( ACTIONS::showDatasheet );
}


void SYMBOL_EDIT_FRAME::buildPreferencesMenu( ACTION_MENU* prefsMenu )
{
    // Modern layout: these items live in the tail of Tools instead of a top-level menu.
    if( UseModernMenuLayout() )
        return;

    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();

    prefsMenu->Add( ACTIONS::configurePaths );
    prefsMenu->Add( ACTIONS::showSymbolLibTable );
    prefsMenu->Add( ACTIONS::openPreferences );

    prefsMenu->AppendSeparator();
    AddMenuLanguageList( prefsMenu, selTool );
}
