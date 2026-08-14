/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2015 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2015 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright (C) 2015 Wayne Stambaugh <stambaughw@gmail.com>
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

#include "footprint_edit_frame.h"
#include "pcbnew_id.h"
#include <advanced_config.h>
#include <bitmaps.h>
#include <tool/actions.h>
#include <tool/action_menu.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_selection_tool.h>
#include <widgets/wx_menubar.h>


void FOOTPRINT_EDIT_FRAME::doReCreateMenuBar()
{
    // KiCad Next: build the shared common menu bar instead of the legacy one when enabled.
    if( UseUnifiedMenuBar() )
    {
        buildCommonMenuBar();
        return;
    }

    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();
    // wxWidgets handles the Mac Application menu behind the scenes, but that means
    // we always have to start from scratch with a new wxMenuBar.
    wxMenuBar*  oldMenuBar = GetMenuBar();
    WX_MENUBAR* menuBar    = new WX_MENUBAR();

    //-- File menu ----------------------------------------------------------
    //
    ACTION_MENU* fileMenu = new ACTION_MENU( false, selTool );

    fileMenu->Add( ACTIONS::newLibrary );
    fileMenu->Add( ACTIONS::addLibrary );
    fileMenu->Add( PCB_ACTIONS::newFootprint );
    fileMenu->Add( PCB_ACTIONS::createFootprint );
    fileMenu->Add( PCB_ACTIONS::editLibFpInFpEditor );

    fileMenu->AppendSeparator();

    fileMenu->Add( ACTIONS::save );
    fileMenu->Add( ACTIONS::saveAs );
    fileMenu->Add( ACTIONS::revert );

    fileMenu->AppendSeparator();

    ACTION_MENU* submenuImport = new ACTION_MENU( false, selTool );
    submenuImport->SetTitle( _( "Import" ) );
    submenuImport->SetIcon( BITMAPS::import );

    submenuImport->Add( PCB_ACTIONS::importFootprint,        ACTION_MENU::NORMAL, _( "Footprint..." ) );
    submenuImport->Add( PCB_ACTIONS::placeImportedGraphics,  ACTION_MENU::NORMAL, _( "Graphics..." ) );

    fileMenu->Add( submenuImport );

    ACTION_MENU* submenuExport = new ACTION_MENU( false, selTool );
    submenuExport->SetTitle( _( "Export" ) );
    submenuExport->SetIcon( BITMAPS::export_file );

    submenuExport->Add( PCB_ACTIONS::exportFootprint, ACTION_MENU::NORMAL, _( "Footprint..." ) );
    submenuExport->Add( _( "View as &PNG..." ),
                        _( "Create a PNG file from the current view" ),
                        ID_FPEDIT_SAVE_PNG,
                        BITMAPS::export_png );

    fileMenu->Add( submenuExport );

    fileMenu->AppendSeparator();
    fileMenu->Add( PCB_ACTIONS::footprintProperties );

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::print );

    fileMenu->AppendSeparator();
    fileMenu->AddClose( _( "Footprint Editor" ) );


    //-- Edit menu -------------------------------------------------------
    //
    ACTION_MENU* editMenu = new ACTION_MENU( false, selTool );

    editMenu->Add( ACTIONS::undo );
    editMenu->Add( ACTIONS::redo );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut );
    editMenu->Add( ACTIONS::copy );
    editMenu->Add( ACTIONS::paste );
    editMenu->Add( ACTIONS::doDelete );
    editMenu->Add( ACTIONS::duplicate );


    editMenu->AppendSeparator();

    // Select Submenu
    ACTION_MENU* selectSubMenu = new ACTION_MENU( false, selTool );
    selectSubMenu->SetTitle( _( "&Select" ) );
    selectSubMenu->Add( ACTIONS::selectAll );
    selectSubMenu->Add( ACTIONS::unselectAll );

    editMenu->Add( selectSubMenu );

    editMenu->AppendSeparator();
    editMenu->Add( PCB_ACTIONS::editTextAndGraphics );
    editMenu->Add( PCB_ACTIONS::padTable );
    editMenu->Add( PCB_ACTIONS::defaultPadProperties );
    editMenu->Add( PCB_ACTIONS::enumeratePads );
    editMenu->Add( ACTIONS::gridOrigin );


    //-- View menu -------------------------------------------------------
    //
    ACTION_MENU* viewMenu = new ACTION_MENU( false, selTool );

    ACTION_MENU* showHidePanels = new ACTION_MENU( false, selTool );
    showHidePanels->SetTitle( _( "Panels" ) );
    buildPanelsMenu( showHidePanels );
    viewMenu->Add( showHidePanels );
    viewMenu->AppendSeparator();

    viewMenu->Add( ACTIONS::showFootprintBrowser );
    viewMenu->Add( ACTIONS::show3DViewer );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomInCenter );
    viewMenu->Add( ACTIONS::zoomOutCenter );
    viewMenu->Add( ACTIONS::zoomFitScreen );
    viewMenu->Add( ACTIONS::zoomTool );
    viewMenu->Add( ACTIONS::zoomRedraw );

    // Modern toolbar preset: the left toolbar is gone, so its display toggles surface here.
    // (Pad / graphics / text display and high contrast already live in the Drawing Mode and
    // Contrast Mode entries of this menu.)
    if( ADVANCED_CFG::GetCfg().m_ModernToolbarLayout )
    {
        viewMenu->AppendSeparator();
        viewMenu->Add( ACTIONS::toggleGrid,            ACTION_MENU::CHECK );
        viewMenu->Add( ACTIONS::toggleGridOverrides,   ACTION_MENU::CHECK );
        viewMenu->Add( PCB_ACTIONS::togglePolarCoords, ACTION_MENU::CHECK );

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

        ACTION_MENU* lineModeSubMenu = new ACTION_MENU( false, selTool );
        lineModeSubMenu->SetTitle( _( "&Line Mode" ) );
        lineModeSubMenu->Add( PCB_ACTIONS::lineModeFree, ACTION_MENU::CHECK );
        lineModeSubMenu->Add( PCB_ACTIONS::lineMode90,   ACTION_MENU::CHECK );
        lineModeSubMenu->Add( PCB_ACTIONS::lineMode45,   ACTION_MENU::CHECK );
        viewMenu->Add( lineModeSubMenu );
    }

    viewMenu->AppendSeparator();
    // Drawing Mode Submenu
    ACTION_MENU* drawingModeSubMenu = new ACTION_MENU( false, selTool );
    drawingModeSubMenu->SetTitle( _( "&Drawing Mode" ) );
    drawingModeSubMenu->SetIcon( BITMAPS::add_zone );

    drawingModeSubMenu->Add( PCB_ACTIONS::padDisplayMode,   ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::graphicsOutlines, ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::textOutlines,     ACTION_MENU::CHECK );
    viewMenu->Add( drawingModeSubMenu );

    // Contrast Mode Submenu
    ACTION_MENU* contrastModeSubMenu = new ACTION_MENU( false, selTool );
    contrastModeSubMenu->SetTitle( _( "&Contrast Mode" ) );
    contrastModeSubMenu->SetIcon( BITMAPS::contrast_mode );

    contrastModeSubMenu->Add( ACTIONS::highContrastMode,    ACTION_MENU::CHECK );
    contrastModeSubMenu->Add( PCB_ACTIONS::layerAlphaDec );
    contrastModeSubMenu->Add( PCB_ACTIONS::layerAlphaInc );
    viewMenu->Add( contrastModeSubMenu );

    viewMenu->Add( PCB_ACTIONS::flipBoard,                  ACTION_MENU::CHECK );


    //-- Place menu -------------------------------------------------------
    //
    ACTION_MENU* placeMenu = new ACTION_MENU( false, selTool );

    placeMenu->Add( PCB_ACTIONS::placePad );
    placeMenu->Add( PCB_ACTIONS::drawRuleArea );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::drawLine );
    placeMenu->Add( PCB_ACTIONS::drawArc );
    placeMenu->Add( PCB_ACTIONS::drawRectangle );
    placeMenu->Add( PCB_ACTIONS::drawCircle );
    placeMenu->Add( PCB_ACTIONS::drawPolygon );
    placeMenu->Add( PCB_ACTIONS::drawBezier );
    placeMenu->Add( PCB_ACTIONS::placeReferenceImage );
    placeMenu->Add( PCB_ACTIONS::placeText );
    placeMenu->Add( PCB_ACTIONS::drawTextBox );
    placeMenu->Add( PCB_ACTIONS::drawTable );
    placeMenu->Add( PCB_ACTIONS::placePoint );
    placeMenu->Add( PCB_ACTIONS::placeBarcode );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::drawOrthogonalDimension );
    placeMenu->Add( PCB_ACTIONS::drawAlignedDimension );
    placeMenu->Add( PCB_ACTIONS::drawCenterDimension );
    placeMenu->Add( PCB_ACTIONS::drawRadialDimension );
    placeMenu->Add( PCB_ACTIONS::drawLeader );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::setAnchor );
    placeMenu->Add( ACTIONS::gridSetOrigin );
    placeMenu->AppendSeparator();
    placeMenu->Add( ACTIONS::gridResetOrigin );


    //-- Inspect menu ------------------------------------------------------
    //
    ACTION_MENU* inspectMenu = new ACTION_MENU( false, selTool );

    inspectMenu->Add( ACTIONS::measureTool );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( PCB_ACTIONS::checkFootprint );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( PCB_ACTIONS::showDatasheet );


    //-- Tools menu --------------------------------------------------------
    //
    ACTION_MENU* toolsMenu = new ACTION_MENU( false, selTool );

    toolsMenu->Add( PCB_ACTIONS::loadFpFromBoard );
    toolsMenu->Add( PCB_ACTIONS::saveFpToBoard );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::cleanupGraphics );
    toolsMenu->Add( PCB_ACTIONS::repairFootprint );


    //-- Preferences menu -------------------------------------------------
    //
    ACTION_MENU* prefsMenu = new ACTION_MENU( false, selTool );

    prefsMenu->Add( ACTIONS::configurePaths );
    prefsMenu->Add( ACTIONS::showFootprintLibTable );
    prefsMenu->Add( ACTIONS::openPreferences );

    prefsMenu->AppendSeparator();
    AddMenuLanguageList( prefsMenu, selTool );

    //--MenuBar -----------------------------------------------------------
    //
    menuBar->Append( fileMenu,    _( "&File" ) );
    menuBar->Append( editMenu,    _( "&Edit" ) );
    menuBar->Append( viewMenu,    _( "&View" ) );
    menuBar->Append( placeMenu,   _( "&Place" ) );
    menuBar->Append( inspectMenu, _( "&Inspect" ) );
    menuBar->Append( toolsMenu,   _( "&Tools" ) );
    menuBar->Append( prefsMenu,   _( "P&references" ) );
    AddStandardHelpMenu( menuBar );

    SetMenuBar( menuBar );
    delete oldMenuBar;
}


//================================ KiCad Next unified menu bar ================================
// The following hooks reproduce the menus built above, but populate a menu supplied by the
// shared EDA_BASE_FRAME::buildCommonMenuBar() orchestrator.  They are used only when the
// m_UnifiedMenuBar advanced flag is set; otherwise the legacy doReCreateMenuBar() above runs.

TOOL_INTERACTIVE* FOOTPRINT_EDIT_FRAME::getCurrentMenuTool()
{
    return m_toolManager->GetTool<PCB_SELECTION_TOOL>();
}


void FOOTPRINT_EDIT_FRAME::buildPanelsMenu( ACTION_MENU* aMenu )
{
    aMenu->Add( ACTIONS::showProperties,        ACTION_MENU::CHECK );
    aMenu->Add( ACTIONS::showLibraryTree,       ACTION_MENU::CHECK );
    aMenu->Add( PCB_ACTIONS::showLayersManager, ACTION_MENU::CHECK );
}


void FOOTPRINT_EDIT_FRAME::buildFileMenu( ACTION_MENU* fileMenu )
{
    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();

    fileMenu->Add( ACTIONS::newLibrary );
    fileMenu->Add( ACTIONS::addLibrary );
    fileMenu->Add( PCB_ACTIONS::newFootprint );
    fileMenu->Add( PCB_ACTIONS::createFootprint );
    fileMenu->Add( PCB_ACTIONS::editLibFpInFpEditor );

    fileMenu->AppendSeparator();

    fileMenu->Add( ACTIONS::save );
    fileMenu->Add( ACTIONS::saveAs );
    fileMenu->Add( ACTIONS::revert );

    fileMenu->AppendSeparator();

    ACTION_MENU* submenuImport = new ACTION_MENU( false, selTool );
    submenuImport->SetTitle( _( "Import" ) );
    submenuImport->SetIcon( BITMAPS::import );

    submenuImport->Add( PCB_ACTIONS::importFootprint,        ACTION_MENU::NORMAL, _( "Footprint..." ) );
    submenuImport->Add( PCB_ACTIONS::placeImportedGraphics,  ACTION_MENU::NORMAL, _( "Graphics..." ) );

    fileMenu->Add( submenuImport );

    ACTION_MENU* submenuExport = new ACTION_MENU( false, selTool );
    submenuExport->SetTitle( _( "Export" ) );
    submenuExport->SetIcon( BITMAPS::export_file );

    submenuExport->Add( PCB_ACTIONS::exportFootprint, ACTION_MENU::NORMAL, _( "Footprint..." ) );
    submenuExport->Add( _( "View as &PNG..." ),
                        _( "Create a PNG file from the current view" ),
                        ID_FPEDIT_SAVE_PNG,
                        BITMAPS::export_png );

    fileMenu->Add( submenuExport );

    fileMenu->AppendSeparator();
    fileMenu->Add( PCB_ACTIONS::footprintProperties );

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::print );

    fileMenu->AppendSeparator();
    fileMenu->AddClose( _( "Footprint Editor" ) );
}


void FOOTPRINT_EDIT_FRAME::buildEditMenu( ACTION_MENU* editMenu )
{
    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();

    editMenu->Add( ACTIONS::undo );
    editMenu->Add( ACTIONS::redo );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut );
    editMenu->Add( ACTIONS::copy );
    editMenu->Add( ACTIONS::paste );
    editMenu->Add( ACTIONS::doDelete );
    editMenu->Add( ACTIONS::duplicate );


    editMenu->AppendSeparator();

    // Select Submenu
    ACTION_MENU* selectSubMenu = new ACTION_MENU( false, selTool );
    selectSubMenu->SetTitle( _( "&Select" ) );
    selectSubMenu->Add( ACTIONS::selectAll );
    selectSubMenu->Add( ACTIONS::unselectAll );

    editMenu->Add( selectSubMenu );

    editMenu->AppendSeparator();
    editMenu->Add( PCB_ACTIONS::editTextAndGraphics );
    editMenu->Add( PCB_ACTIONS::padTable );
    editMenu->Add( PCB_ACTIONS::defaultPadProperties );
    editMenu->Add( PCB_ACTIONS::enumeratePads );
    editMenu->Add( ACTIONS::gridOrigin );
}


void FOOTPRINT_EDIT_FRAME::buildViewMenu( ACTION_MENU* viewMenu )
{
    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();

    ACTION_MENU* showHidePanels = new ACTION_MENU( false, selTool );
    showHidePanels->SetTitle( _( "Panels" ) );
    buildPanelsMenu( showHidePanels );
    viewMenu->Add( showHidePanels );
    viewMenu->AppendSeparator();

    viewMenu->Add( ACTIONS::showFootprintBrowser );
    viewMenu->Add( ACTIONS::show3DViewer );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomInCenter );
    viewMenu->Add( ACTIONS::zoomOutCenter );
    viewMenu->Add( ACTIONS::zoomFitScreen );
    viewMenu->Add( ACTIONS::zoomTool );
    viewMenu->Add( ACTIONS::zoomRedraw );

    // Modern toolbar preset: the left toolbar is gone, so its display toggles surface here.
    // (Pad / graphics / text display and high contrast already live in the Drawing Mode and
    // Contrast Mode entries of this menu.)
    if( ADVANCED_CFG::GetCfg().m_ModernToolbarLayout )
    {
        viewMenu->AppendSeparator();
        viewMenu->Add( ACTIONS::toggleGrid,            ACTION_MENU::CHECK );
        viewMenu->Add( ACTIONS::toggleGridOverrides,   ACTION_MENU::CHECK );
        viewMenu->Add( PCB_ACTIONS::togglePolarCoords, ACTION_MENU::CHECK );

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

        ACTION_MENU* lineModeSubMenu = new ACTION_MENU( false, selTool );
        lineModeSubMenu->SetTitle( _( "&Line Mode" ) );
        lineModeSubMenu->Add( PCB_ACTIONS::lineModeFree, ACTION_MENU::CHECK );
        lineModeSubMenu->Add( PCB_ACTIONS::lineMode90,   ACTION_MENU::CHECK );
        lineModeSubMenu->Add( PCB_ACTIONS::lineMode45,   ACTION_MENU::CHECK );
        viewMenu->Add( lineModeSubMenu );
    }

    viewMenu->AppendSeparator();
    // Drawing Mode Submenu
    ACTION_MENU* drawingModeSubMenu = new ACTION_MENU( false, selTool );
    drawingModeSubMenu->SetTitle( _( "&Drawing Mode" ) );
    drawingModeSubMenu->SetIcon( BITMAPS::add_zone );

    drawingModeSubMenu->Add( PCB_ACTIONS::padDisplayMode,   ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::graphicsOutlines, ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::textOutlines,     ACTION_MENU::CHECK );
    viewMenu->Add( drawingModeSubMenu );

    // Contrast Mode Submenu
    ACTION_MENU* contrastModeSubMenu = new ACTION_MENU( false, selTool );
    contrastModeSubMenu->SetTitle( _( "&Contrast Mode" ) );
    contrastModeSubMenu->SetIcon( BITMAPS::contrast_mode );

    contrastModeSubMenu->Add( ACTIONS::highContrastMode,    ACTION_MENU::CHECK );
    contrastModeSubMenu->Add( PCB_ACTIONS::layerAlphaDec );
    contrastModeSubMenu->Add( PCB_ACTIONS::layerAlphaInc );
    viewMenu->Add( contrastModeSubMenu );

    viewMenu->Add( PCB_ACTIONS::flipBoard,                  ACTION_MENU::CHECK );
}


void FOOTPRINT_EDIT_FRAME::buildPlaceMenu( ACTION_MENU* placeMenu )
{
    placeMenu->Add( PCB_ACTIONS::placePad );
    placeMenu->Add( PCB_ACTIONS::drawRuleArea );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::drawLine );
    placeMenu->Add( PCB_ACTIONS::drawArc );
    placeMenu->Add( PCB_ACTIONS::drawRectangle );
    placeMenu->Add( PCB_ACTIONS::drawCircle );
    placeMenu->Add( PCB_ACTIONS::drawPolygon );
    placeMenu->Add( PCB_ACTIONS::drawBezier );
    placeMenu->Add( PCB_ACTIONS::placeReferenceImage );
    placeMenu->Add( PCB_ACTIONS::placeText );
    placeMenu->Add( PCB_ACTIONS::drawTextBox );
    placeMenu->Add( PCB_ACTIONS::drawTable );
    placeMenu->Add( PCB_ACTIONS::placePoint );
    placeMenu->Add( PCB_ACTIONS::placeBarcode );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::drawOrthogonalDimension );
    placeMenu->Add( PCB_ACTIONS::drawAlignedDimension );
    placeMenu->Add( PCB_ACTIONS::drawCenterDimension );
    placeMenu->Add( PCB_ACTIONS::drawRadialDimension );
    placeMenu->Add( PCB_ACTIONS::drawLeader );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::setAnchor );
    placeMenu->Add( ACTIONS::gridSetOrigin );
    placeMenu->AppendSeparator();
    placeMenu->Add( ACTIONS::gridResetOrigin );
}


void FOOTPRINT_EDIT_FRAME::buildInspectMenu( ACTION_MENU* inspectMenu )
{
    // Modern layout: the footprint checker moves to Tools and the datasheet / measure tool
    // to Reports, so the classic Inspect menu is dropped entirely.
    if( UseModernMenuLayout() )
        return;

    inspectMenu->Add( ACTIONS::measureTool );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( PCB_ACTIONS::checkFootprint );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( PCB_ACTIONS::showDatasheet );
}


void FOOTPRINT_EDIT_FRAME::buildToolsMenu( ACTION_MENU* toolsMenu )
{
    if( UseModernMenuLayout() )
    {
        // Altium-style Tools: checker, board exchange and cleanup, with the Preferences
        // items folded into the tail until the title-bar gear hosts them.
        PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();

        toolsMenu->Add( PCB_ACTIONS::checkFootprint );

        toolsMenu->AppendSeparator();
        toolsMenu->Add( PCB_ACTIONS::loadFpFromBoard );
        toolsMenu->Add( PCB_ACTIONS::saveFpToBoard );

        toolsMenu->AppendSeparator();
        toolsMenu->Add( PCB_ACTIONS::cleanupGraphics );
        toolsMenu->Add( PCB_ACTIONS::repairFootprint );

        toolsMenu->AppendSeparator();
        toolsMenu->Add( ACTIONS::configurePaths );
        toolsMenu->Add( ACTIONS::showFootprintLibTable );
        toolsMenu->Add( ACTIONS::openPreferences );

        toolsMenu->AppendSeparator();
        AddMenuLanguageList( toolsMenu, selTool );
        return;
    }

    toolsMenu->Add( PCB_ACTIONS::loadFpFromBoard );
    toolsMenu->Add( PCB_ACTIONS::saveFpToBoard );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::cleanupGraphics );
    toolsMenu->Add( PCB_ACTIONS::repairFootprint );
}


void FOOTPRINT_EDIT_FRAME::buildReportsMenu( ACTION_MENU* reportsMenu )
{
    reportsMenu->Add( PCB_ACTIONS::showDatasheet );

    reportsMenu->AppendSeparator();
    reportsMenu->Add( ACTIONS::measureTool );
}


void FOOTPRINT_EDIT_FRAME::buildPreferencesMenu( ACTION_MENU* prefsMenu )
{
    // Modern layout: these items live in the tail of Tools instead of a top-level menu.
    if( UseModernMenuLayout() )
        return;

    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();

    prefsMenu->Add( ACTIONS::configurePaths );
    prefsMenu->Add( ACTIONS::showFootprintLibTable );
    prefsMenu->Add( ACTIONS::openPreferences );

    prefsMenu->AppendSeparator();
    AddMenuLanguageList( prefsMenu, selTool );
}
