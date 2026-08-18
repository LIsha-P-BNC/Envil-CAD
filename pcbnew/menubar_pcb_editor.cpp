/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2017 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2012 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright (C) 2012 Wayne Stambaugh <stambaughw@gmail.com>
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

#include <advanced_config.h>
#include <bitmaps.h>
#include <file_history.h>
#include <kiface_base.h>
#include <pcb_edit_frame.h>
#include <pcbnew_id.h>
#include <tool/action_manager.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_selection_tool.h>
#include <widgets/wx_menubar.h>


void PCB_EDIT_FRAME::doReCreateMenuBar()
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

    // Recreate all menus:

    //-- File menu -----------------------------------------------------------
    //
    ACTION_MENU*   fileMenu = new ACTION_MENU( false, selTool );
    static ACTION_MENU* openRecentMenu;

    if( Kiface().IsSingle() )   // not when under a project mgr
    {
        FILE_HISTORY& fileHistory = GetFileHistory();

        // Create the menu if it does not exist. Adding a file to/from the history
        // will automatically refresh the menu.
        if( !openRecentMenu )
        {
            openRecentMenu = new ACTION_MENU( false, selTool );
            openRecentMenu->SetIcon( BITMAPS::recent );

            fileHistory.UseMenu( openRecentMenu );
            fileHistory.AddFilesToMenu();
        }

        // Ensure the title is up to date after changing language
        openRecentMenu->SetTitle( _( "Open Recent" ) );
        fileHistory.UpdateClearText( openRecentMenu, _( "Clear Recent Files" ) );

        fileMenu->Add( ACTIONS::doNew );
        fileMenu->Add( ACTIONS::open );

        wxMenuItem* item = fileMenu->Add( openRecentMenu->Clone() );

        // Add the file menu condition here since it needs the item ID for the submenu
        ACTION_CONDITIONS cond;
        cond.Enable( FILE_HISTORY::FileHistoryNotEmpty( fileHistory ) );
        RegisterUIUpdateHandler( item->GetId(), cond );
    }

    fileMenu->Add( PCB_ACTIONS::appendBoard );
    fileMenu->AppendSeparator();

    fileMenu->Add( ACTIONS::save );

    // Save as menu:
    // under a project mgr we do not want to modify the board filename
    // to keep consistency with the project mgr which expects files names same as prj name
    // for main files
    if( Kiface().IsSingle() )
        fileMenu->Add( ACTIONS::saveAs );
    else
        fileMenu->Add( ACTIONS::saveCopy );

    fileMenu->Add( ACTIONS::revert );

    fileMenu->AppendSeparator();

    // Import submenu
    ACTION_MENU* submenuImport = new ACTION_MENU( false, selTool );
    submenuImport->SetTitle( _( "Import" ) );
    submenuImport->SetIcon( BITMAPS::import );

    submenuImport->Add( PCB_ACTIONS::importNetlist,          ACTION_MENU::NORMAL, _( "Netlist..." ) );
    submenuImport->Add( PCB_ACTIONS::importSpecctraSession,  ACTION_MENU::NORMAL, _( "Specctra Session..." ) );
    submenuImport->Add( PCB_ACTIONS::placeImportedGraphics,  ACTION_MENU::NORMAL, _( "Graphics..." ) );
    submenuImport->Add( PCB_ACTIONS::openNonKicadBoard );

    fileMenu->AppendSeparator();
    fileMenu->Add( submenuImport );

    // Export submenu
    ACTION_MENU* submenuExport = new ACTION_MENU( false, selTool );
    submenuExport->SetTitle( _( "Export" ) );
    submenuExport->SetIcon( BITMAPS::export_file );

    submenuExport->Add( PCB_ACTIONS::exportSpecctraDSN, ACTION_MENU::NORMAL, _( "Specctra DSN..." ) );
    submenuExport->Add( PCB_ACTIONS::exportGenCAD,      ACTION_MENU::NORMAL, _( "GenCAD..." ) );
    submenuExport->Add( PCB_ACTIONS::exportVRML,        ACTION_MENU::NORMAL, _( "VRML..." ) );
    submenuExport->Add( PCB_ACTIONS::exportIDF,         ACTION_MENU::NORMAL, _( "IDFv3..." ) );
    submenuExport->Add( PCB_ACTIONS::exportSTEP,        ACTION_MENU::NORMAL, _( "STEP/GLB/BREP/XAO/PLY/STL..." ) );
    submenuExport->Add( PCB_ACTIONS::exportCmpFile,     ACTION_MENU::NORMAL, _( "Footprint Association (.cmp) File..." ) );
    submenuExport->Add( PCB_ACTIONS::exportHyperlynx,   ACTION_MENU::NORMAL, _( "Hyperlynx..." ) );

    if( ADVANCED_CFG::GetCfg().m_ShowPcbnewExportNetlist && m_exportNetlistAction )
        submenuExport->Add( *m_exportNetlistAction );

    submenuExport->AppendSeparator();
    submenuExport->Add( PCB_ACTIONS::exportFootprints,  ACTION_MENU::NORMAL, _( "Footprints..." ) );

    fileMenu->Add( submenuExport );

    // Fabrication Outputs submenu
    ACTION_MENU* submenuFabOutputs = new ACTION_MENU( false, selTool );
    submenuFabOutputs->SetTitle( _( "Fabrication Outputs" ) );
    submenuFabOutputs->SetIcon( BITMAPS::fabrication );

    submenuFabOutputs->Add( PCB_ACTIONS::generateGerbers );
    submenuFabOutputs->Add( PCB_ACTIONS::generateDrillFiles );
    submenuFabOutputs->Add( PCB_ACTIONS::generateIPC2581File );
    submenuFabOutputs->Add( PCB_ACTIONS::generateODBPPFile );

    submenuFabOutputs->Add( PCB_ACTIONS::generatePosFile );
    submenuFabOutputs->Add( PCB_ACTIONS::generateReportFile );
    submenuFabOutputs->Add( PCB_ACTIONS::generateD356File );
    submenuFabOutputs->Add( PCB_ACTIONS::generateBOM );
    fileMenu->Add( submenuFabOutputs );

    fileMenu->AppendSeparator();
    fileMenu->Add( PCB_ACTIONS::boardSetup );

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::pageSettings );
    fileMenu->Add( ACTIONS::print );
    fileMenu->Add( ACTIONS::plot );

    fileMenu->AppendSeparator();
    fileMenu->AddQuitOrClose( &Kiface(), _( "PCB Editor" ) );

    //-- Edit menu -----------------------------------------------------------
    //
    ACTION_MENU* editMenu = new ACTION_MENU( false, selTool );

    editMenu->Add( ACTIONS::undo );
    editMenu->Add( ACTIONS::redo );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut );
    editMenu->Add( ACTIONS::copy );
    editMenu->Add( ACTIONS::paste );
    editMenu->Add( ACTIONS::pasteSpecial );
    editMenu->Add( ACTIONS::doDelete );

    editMenu->AppendSeparator();

    // Select Submenu
    ACTION_MENU* selectSubMenu = new ACTION_MENU( false, selTool );
    selectSubMenu->SetTitle( _( "&Select" ) );

    selectSubMenu->Add( ACTIONS::selectAll );
    selectSubMenu->Add( ACTIONS::unselectAll );

    editMenu->Add( selectSubMenu );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::find );
    editMenu->Add( PCB_ACTIONS::findByProperties );

    editMenu->AppendSeparator();
    editMenu->Add( PCB_ACTIONS::editTracksAndVias );
    editMenu->Add( PCB_ACTIONS::editTextAndGraphics );
    editMenu->Add( PCB_ACTIONS::editTeardrops );
    editMenu->Add( PCB_ACTIONS::changeFootprints );
    editMenu->Add( PCB_ACTIONS::swapLayers );
    editMenu->Add( ACTIONS::gridOrigin );

    editMenu->AppendSeparator();
    editMenu->Add( PCB_ACTIONS::zoneFillAll );
    editMenu->Add( PCB_ACTIONS::zoneUnfillAll );
    editMenu->Add( PCB_ACTIONS::regenerateAllTuning );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::deleteTool );
    editMenu->Add( PCB_ACTIONS::globalDeletions );


    //----- View menu -----------------------------------------------------------
    //
    ACTION_MENU* viewMenu = new ACTION_MENU( false, selTool );

    // Show / Hide Panels submenu (content shared with the shell's Panels button).
    ACTION_MENU* showHidePanels = new ACTION_MENU( false, selTool );
    showHidePanels->SetTitle( _( "Panels" ) );
    buildPanelsMenu( showHidePanels );

    viewMenu->Add( showHidePanels );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::showFootprintBrowser );
    viewMenu->Add( ACTIONS::show3DViewer );
    if( !( ADVANCED_CFG::GetCfg().m_SingleWindowShell
           && ADVANCED_CFG::GetCfg().m_CommonAiPanel ) )
        viewMenu->Add( PCB_ACTIONS::showAiChat );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomInCenter );
    viewMenu->Add( ACTIONS::zoomOutCenter );
    viewMenu->Add( ACTIONS::zoomFitScreen );
    viewMenu->Add( ACTIONS::zoomFitObjects );
    viewMenu->Add( ACTIONS::zoomFitSelection );
    viewMenu->Add( ACTIONS::zoomTool );
    viewMenu->Add( ACTIONS::zoomRedraw );

    // Modern toolbar preset: the left toolbar is gone, so its display toggles surface here.
    // (High contrast, zone / pad / via / track display and the panel toggles already live in
    // the Contrast Mode, Drawing Mode and Panels entries of this menu.)
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

        viewMenu->Add( PCB_ACTIONS::showRatsnest,       ACTION_MENU::CHECK );
        viewMenu->Add( PCB_ACTIONS::ratsnestLineMode,   ACTION_MENU::CHECK );
        viewMenu->Add( PCB_ACTIONS::toggleNetHighlight, ACTION_MENU::CHECK );

        if( ADVANCED_CFG::GetCfg().m_DrawBoundingBoxes )
            viewMenu->Add( ACTIONS::toggleBoundingBoxes, ACTION_MENU::CHECK );
    }

    viewMenu->AppendSeparator();
    // Drawing Mode Submenu
    ACTION_MENU* drawingModeSubMenu = new ACTION_MENU( false, selTool );
    drawingModeSubMenu->SetTitle( _( "&Drawing Mode" ) );
    drawingModeSubMenu->SetIcon( BITMAPS::add_zone );

    drawingModeSubMenu->Add( PCB_ACTIONS::zoneDisplayFilled,   ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::zoneDisplayOutline,  ACTION_MENU::CHECK );

    if( ADVANCED_CFG::GetCfg().m_ExtraZoneDisplayModes )
    {
        drawingModeSubMenu->Add( PCB_ACTIONS::zoneDisplayFractured,    ACTION_MENU::CHECK );
        drawingModeSubMenu->Add( PCB_ACTIONS::zoneDisplayTriangulated, ACTION_MENU::CHECK );
    }

    drawingModeSubMenu->AppendSeparator();
    drawingModeSubMenu->Add( PCB_ACTIONS::padDisplayMode,      ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::viaDisplayMode,      ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::trackDisplayMode,    ACTION_MENU::CHECK );

    drawingModeSubMenu->AppendSeparator();
    drawingModeSubMenu->Add( PCB_ACTIONS::graphicsOutlines,    ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::textOutlines,        ACTION_MENU::CHECK );

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

#ifdef __APPLE__
    viewMenu->AppendSeparator();
#endif

    //-- Place Menu ----------------------------------------------------------
    //
    ACTION_MENU* placeMenu = new ACTION_MENU( false, selTool );

    placeMenu->Add( PCB_ACTIONS::placeFootprint );
    placeMenu->Add( PCB_ACTIONS::drawVia );
    placeMenu->Add( PCB_ACTIONS::drawZone );
    placeMenu->Add( PCB_ACTIONS::drawRuleArea );

    ACTION_MENU* muwaveSubmenu = new ACTION_MENU( false, selTool );
    muwaveSubmenu->SetTitle( _( "Draw Microwave Shapes" ) );
    muwaveSubmenu->SetIcon( BITMAPS::mw_add_line );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateLine );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateGap );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateStub );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateStubArc );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateFunctionShape );
    placeMenu->Add( muwaveSubmenu );

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
    ACTION_MENU* dimensionSubmenu = new ACTION_MENU( false, selTool );
    dimensionSubmenu->SetTitle( _( "Draw Dimensions" ) );
    dimensionSubmenu->SetIcon( BITMAPS::add_aligned_dimension );
    dimensionSubmenu->Add( PCB_ACTIONS::drawOrthogonalDimension );
    dimensionSubmenu->Add( PCB_ACTIONS::drawAlignedDimension );
    dimensionSubmenu->Add( PCB_ACTIONS::drawCenterDimension );
    dimensionSubmenu->Add( PCB_ACTIONS::drawRadialDimension );
    dimensionSubmenu->Add( PCB_ACTIONS::drawLeader );
    placeMenu->Add( dimensionSubmenu );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::placeCharacteristics );
    placeMenu->Add( PCB_ACTIONS::placeStackup );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::drillOrigin );
    placeMenu->Add( PCB_ACTIONS::drillResetOrigin );
    placeMenu->Add( ACTIONS::gridSetOrigin );
    placeMenu->Add( ACTIONS::gridResetOrigin );

    placeMenu->AppendSeparator();
    ACTION_MENU* autoplaceSubmenu = new ACTION_MENU( false, selTool );
    autoplaceSubmenu->SetTitle( _( "Auto-Place Footprints" ) );
    autoplaceSubmenu->SetIcon( BITMAPS::mode_module );

    autoplaceSubmenu->Add( PCB_ACTIONS::autoplaceOffboardComponents );
    autoplaceSubmenu->Add( PCB_ACTIONS::autoplaceSelectedComponents );

    placeMenu->Add( autoplaceSubmenu );

    //-- Route Menu ----------------------------------------------------------
    //
    ACTION_MENU* routeMenu = new ACTION_MENU( false, selTool );

    routeMenu->Add( PCB_ACTIONS::selectLayerPair );

    routeMenu->AppendSeparator();
    routeMenu->Add( PCB_ACTIONS::routeSingleTrack );
    routeMenu->Add( PCB_ACTIONS::routeDiffPair );

    routeMenu->AppendSeparator();
    routeMenu->Add( PCB_ACTIONS::tuneSingleTrack );
    routeMenu->Add( PCB_ACTIONS::tuneDiffPair );
    routeMenu->Add( PCB_ACTIONS::tuneSkew );

    routeMenu->AppendSeparator();
    routeMenu->Add( PCB_ACTIONS::routerSettingsDialog );


    //-- Inspect Menu --------------------------------------------------------
    //
    ACTION_MENU* inspectMenu = new ACTION_MENU( false, selTool );

    inspectMenu->Add( PCB_ACTIONS::boardStatistics );
    inspectMenu->Add( ACTIONS::measureTool );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( PCB_ACTIONS::runDRC );
    inspectMenu->Add( ACTIONS::prevMarker );
    inspectMenu->Add( ACTIONS::nextMarker );
    inspectMenu->Add( ACTIONS::excludeMarker );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( PCB_ACTIONS::inspectClearance );
    inspectMenu->Add( PCB_ACTIONS::inspectConstraints );
    inspectMenu->Add( PCB_ACTIONS::showFootprintAssociations );
    inspectMenu->Add( PCB_ACTIONS::diffFootprint );


    //-- Tools menu ----------------------------------------------------------
    //
    ACTION_MENU* toolsMenu = new ACTION_MENU( false, selTool );

    toolsMenu->Add( ACTIONS::updatePcbFromSchematic )->Enable( !Kiface().IsSingle() );
    toolsMenu->Add( PCB_ACTIONS::showEeschema );

    if( !Kiface().IsSingle() )
        toolsMenu->Add( ACTIONS::showProjectManager );

    toolsMenu->Add( ACTIONS::showCalculatorTools );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::drcRuleEditor );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::showFootprintEditor );
    toolsMenu->Add( PCB_ACTIONS::updateFootprints );

    //Zones management
    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::zonesManager );

    if( ADVANCED_CFG::GetCfg().m_EnableGenerators )
    {
        toolsMenu->AppendSeparator();
        toolsMenu->Add( PCB_ACTIONS::generatorsShowManager );
        toolsMenu->Add( PCB_ACTIONS::regenerateAll );
        toolsMenu->Add( PCB_ACTIONS::regenerateSelected );
    }

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::cleanupTracksAndVias );
    toolsMenu->Add( PCB_ACTIONS::removeUnusedPads );
    toolsMenu->Add( PCB_ACTIONS::cleanupGraphics );
    toolsMenu->Add( PCB_ACTIONS::repairBoard );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::collect3DModels );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::boardReannotate );
    toolsMenu->Add( ACTIONS::updateSchematicFromPcb )->Enable( !Kiface().IsSingle() );

    ACTION_MENU* multichannelSubmenu = new ACTION_MENU( false, selTool );
    multichannelSubmenu->SetTitle( _( "Multi-Channel" ) );
    multichannelSubmenu->SetIcon( BITMAPS::mode_module );
    multichannelSubmenu->Add( PCB_ACTIONS::generatePlacementRuleAreas );
    multichannelSubmenu->Add( PCB_ACTIONS::repeatLayout );

    toolsMenu->Add( multichannelSubmenu );

    // Anvil: the External Plugins submenu was removed — its entries (reload / show folder)
    // belong to the SWIG action-plugin backend, which does not exist in this fork, so both
    // menu items were dead (no handler).  IPC-API plugins surface on the toolbar instead.

    //-- Preferences menu ----------------------------------------------------
    //
    ACTION_MENU* prefsMenu = new ACTION_MENU( false, selTool );

    prefsMenu->Add( ACTIONS::configurePaths );
    prefsMenu->Add( ACTIONS::showFootprintLibTable );

    if( ADVANCED_CFG::GetCfg().m_EnablePcbDesignBlocks )
        prefsMenu->Add( ACTIONS::showDesignBlockLibTable );

    prefsMenu->Add( ACTIONS::openPreferences );

    prefsMenu->AppendSeparator();
    AddMenuLanguageList( prefsMenu, selTool );


    //--MenuBar -----------------------------------------------------------
    //
    menuBar->Append( fileMenu,    _( "&File" ) );
    menuBar->Append( editMenu,    _( "&Edit" ) );
    menuBar->Append( viewMenu,    _( "&View" ) );
    menuBar->Append( placeMenu,   _( "&Place" ) );
    menuBar->Append( routeMenu,   _( "Ro&ute" ) );
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

TOOL_INTERACTIVE* PCB_EDIT_FRAME::getCurrentMenuTool()
{
    return m_toolManager->GetTool<PCB_SELECTION_TOOL>();
}


void PCB_EDIT_FRAME::buildPanelsMenu( ACTION_MENU* aMenu )
{
    aMenu->Add( ACTIONS::showProperties,        ACTION_MENU::CHECK );
    aMenu->Add( PCB_ACTIONS::showSearch,        ACTION_MENU::CHECK );
    aMenu->Add( PCB_ACTIONS::showLayersManager, ACTION_MENU::CHECK );
    aMenu->Add( PCB_ACTIONS::showNetInspector,  ACTION_MENU::CHECK );

    // In the shell's common-AI mode the shell owns the only AI panel, so drop the per-editor
    // AI toggle (kept when CommonAiPanel is off).
    if( !( ADVANCED_CFG::GetCfg().m_SingleWindowShell
           && ADVANCED_CFG::GetCfg().m_CommonAiPanel ) )
        aMenu->Add( PCB_ACTIONS::showAiChat );

    if( ADVANCED_CFG::GetCfg().m_EnablePcbDesignBlocks )
        aMenu->Add( PCB_ACTIONS::showDesignBlockPanel, ACTION_MENU::CHECK, _( "Design Blocks" ) );
}


void PCB_EDIT_FRAME::buildFileMenu( ACTION_MENU* fileMenu )
{
    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();
    static ACTION_MENU* openRecentMenu;

    if( Kiface().IsSingle() )   // not when under a project mgr
    {
        FILE_HISTORY& fileHistory = GetFileHistory();

        // Create the menu if it does not exist. Adding a file to/from the history
        // will automatically refresh the menu.
        if( !openRecentMenu )
        {
            openRecentMenu = new ACTION_MENU( false, selTool );
            openRecentMenu->SetIcon( BITMAPS::recent );

            fileHistory.UseMenu( openRecentMenu );
            fileHistory.AddFilesToMenu();
        }

        // Ensure the title is up to date after changing language
        openRecentMenu->SetTitle( _( "Open Recent" ) );
        fileHistory.UpdateClearText( openRecentMenu, _( "Clear Recent Files" ) );

        fileMenu->Add( ACTIONS::doNew );
        fileMenu->Add( ACTIONS::open );

        wxMenuItem* item = fileMenu->Add( openRecentMenu->Clone() );

        // Add the file menu condition here since it needs the item ID for the submenu
        ACTION_CONDITIONS cond;
        cond.Enable( FILE_HISTORY::FileHistoryNotEmpty( fileHistory ) );
        RegisterUIUpdateHandler( item->GetId(), cond );
    }

    fileMenu->Add( PCB_ACTIONS::appendBoard );
    fileMenu->AppendSeparator();

    fileMenu->Add( ACTIONS::save );

    // Save as menu:
    // under a project mgr we do not want to modify the board filename
    // to keep consistency with the project mgr which expects files names same as prj name
    // for main files
    if( Kiface().IsSingle() )
        fileMenu->Add( ACTIONS::saveAs );
    else
        fileMenu->Add( ACTIONS::saveCopy );

    fileMenu->Add( ACTIONS::revert );

    fileMenu->AppendSeparator();

    // Import submenu
    ACTION_MENU* submenuImport = new ACTION_MENU( false, selTool );
    submenuImport->SetTitle( _( "Import" ) );
    submenuImport->SetIcon( BITMAPS::import );

    submenuImport->Add( PCB_ACTIONS::importNetlist,          ACTION_MENU::NORMAL, _( "Netlist..." ) );
    submenuImport->Add( PCB_ACTIONS::importSpecctraSession,  ACTION_MENU::NORMAL, _( "Specctra Session..." ) );
    submenuImport->Add( PCB_ACTIONS::placeImportedGraphics,  ACTION_MENU::NORMAL, _( "Graphics..." ) );
    submenuImport->Add( PCB_ACTIONS::openNonKicadBoard );

    fileMenu->AppendSeparator();
    fileMenu->Add( submenuImport );

    // Export submenu
    ACTION_MENU* submenuExport = new ACTION_MENU( false, selTool );
    submenuExport->SetTitle( _( "Export" ) );
    submenuExport->SetIcon( BITMAPS::export_file );

    submenuExport->Add( PCB_ACTIONS::exportSpecctraDSN, ACTION_MENU::NORMAL, _( "Specctra DSN..." ) );
    submenuExport->Add( PCB_ACTIONS::exportGenCAD,      ACTION_MENU::NORMAL, _( "GenCAD..." ) );
    submenuExport->Add( PCB_ACTIONS::exportVRML,        ACTION_MENU::NORMAL, _( "VRML..." ) );
    submenuExport->Add( PCB_ACTIONS::exportIDF,         ACTION_MENU::NORMAL, _( "IDFv3..." ) );
    submenuExport->Add( PCB_ACTIONS::exportSTEP,        ACTION_MENU::NORMAL, _( "STEP/GLB/BREP/XAO/PLY/STL..." ) );
    submenuExport->Add( PCB_ACTIONS::exportCmpFile,     ACTION_MENU::NORMAL, _( "Footprint Association (.cmp) File..." ) );
    submenuExport->Add( PCB_ACTIONS::exportHyperlynx,   ACTION_MENU::NORMAL, _( "Hyperlynx..." ) );

    if( ADVANCED_CFG::GetCfg().m_ShowPcbnewExportNetlist && m_exportNetlistAction )
        submenuExport->Add( *m_exportNetlistAction );

    submenuExport->AppendSeparator();
    submenuExport->Add( PCB_ACTIONS::exportFootprints,  ACTION_MENU::NORMAL, _( "Footprints..." ) );

    fileMenu->Add( submenuExport );

    // Fabrication Outputs submenu
    ACTION_MENU* submenuFabOutputs = new ACTION_MENU( false, selTool );
    submenuFabOutputs->SetTitle( _( "Fabrication Outputs" ) );
    submenuFabOutputs->SetIcon( BITMAPS::fabrication );

    submenuFabOutputs->Add( PCB_ACTIONS::generateGerbers );
    submenuFabOutputs->Add( PCB_ACTIONS::generateDrillFiles );
    submenuFabOutputs->Add( PCB_ACTIONS::generateIPC2581File );
    submenuFabOutputs->Add( PCB_ACTIONS::generateODBPPFile );

    submenuFabOutputs->Add( PCB_ACTIONS::generatePosFile );
    submenuFabOutputs->Add( PCB_ACTIONS::generateReportFile );
    submenuFabOutputs->Add( PCB_ACTIONS::generateD356File );
    submenuFabOutputs->Add( PCB_ACTIONS::generateBOM );
    fileMenu->Add( submenuFabOutputs );

    // Modern layout: Board Setup lives in the Design menu instead.
    if( !UseModernMenuLayout() )
    {
        fileMenu->AppendSeparator();
        fileMenu->Add( PCB_ACTIONS::boardSetup );
    }

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::pageSettings );
    fileMenu->Add( ACTIONS::print );
    fileMenu->Add( ACTIONS::plot );

    fileMenu->AppendSeparator();
    fileMenu->AddQuitOrClose( &Kiface(), _( "PCB Editor" ) );
}


void PCB_EDIT_FRAME::buildEditMenu( ACTION_MENU* editMenu )
{
    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();

    editMenu->Add( ACTIONS::undo );
    editMenu->Add( ACTIONS::redo );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut );
    editMenu->Add( ACTIONS::copy );
    editMenu->Add( ACTIONS::paste );
    editMenu->Add( ACTIONS::pasteSpecial );
    editMenu->Add( ACTIONS::doDelete );

    editMenu->AppendSeparator();

    // Select Submenu
    ACTION_MENU* selectSubMenu = new ACTION_MENU( false, selTool );
    selectSubMenu->SetTitle( _( "&Select" ) );

    selectSubMenu->Add( ACTIONS::selectAll );
    selectSubMenu->Add( ACTIONS::unselectAll );

    editMenu->Add( selectSubMenu );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::find );
    editMenu->Add( PCB_ACTIONS::findByProperties );

    editMenu->AppendSeparator();
    editMenu->Add( PCB_ACTIONS::editTracksAndVias );
    editMenu->Add( PCB_ACTIONS::editTextAndGraphics );
    editMenu->Add( PCB_ACTIONS::editTeardrops );
    editMenu->Add( PCB_ACTIONS::changeFootprints );

    // Modern layout: Swap Layers lives in the Design menu instead.
    if( !UseModernMenuLayout() )
        editMenu->Add( PCB_ACTIONS::swapLayers );

    editMenu->Add( ACTIONS::gridOrigin );

    editMenu->AppendSeparator();
    editMenu->Add( PCB_ACTIONS::zoneFillAll );
    editMenu->Add( PCB_ACTIONS::zoneUnfillAll );
    editMenu->Add( PCB_ACTIONS::regenerateAllTuning );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::deleteTool );
    editMenu->Add( PCB_ACTIONS::globalDeletions );
}


void PCB_EDIT_FRAME::buildViewMenu( ACTION_MENU* viewMenu )
{
    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();

    // Show / Hide Panels submenu (content shared with the shell's Panels button).
    ACTION_MENU* showHidePanels = new ACTION_MENU( false, selTool );
    showHidePanels->SetTitle( _( "Panels" ) );
    buildPanelsMenu( showHidePanels );

    viewMenu->Add( showHidePanels );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::showFootprintBrowser );
    viewMenu->Add( ACTIONS::show3DViewer );
    if( !( ADVANCED_CFG::GetCfg().m_SingleWindowShell
           && ADVANCED_CFG::GetCfg().m_CommonAiPanel ) )
        viewMenu->Add( PCB_ACTIONS::showAiChat );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomInCenter );
    viewMenu->Add( ACTIONS::zoomOutCenter );
    viewMenu->Add( ACTIONS::zoomFitScreen );
    viewMenu->Add( ACTIONS::zoomFitObjects );
    viewMenu->Add( ACTIONS::zoomFitSelection );
    viewMenu->Add( ACTIONS::zoomTool );
    viewMenu->Add( ACTIONS::zoomRedraw );

    // Modern toolbar preset: the left toolbar is gone, so its display toggles surface here.
    // (High contrast, zone / pad / via / track display and the panel toggles already live in
    // the Contrast Mode, Drawing Mode and Panels entries of this menu.)
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

        viewMenu->Add( PCB_ACTIONS::showRatsnest,       ACTION_MENU::CHECK );
        viewMenu->Add( PCB_ACTIONS::ratsnestLineMode,   ACTION_MENU::CHECK );
        viewMenu->Add( PCB_ACTIONS::toggleNetHighlight, ACTION_MENU::CHECK );

        if( ADVANCED_CFG::GetCfg().m_DrawBoundingBoxes )
            viewMenu->Add( ACTIONS::toggleBoundingBoxes, ACTION_MENU::CHECK );
    }

    viewMenu->AppendSeparator();
    // Drawing Mode Submenu
    ACTION_MENU* drawingModeSubMenu = new ACTION_MENU( false, selTool );
    drawingModeSubMenu->SetTitle( _( "&Drawing Mode" ) );
    drawingModeSubMenu->SetIcon( BITMAPS::add_zone );

    drawingModeSubMenu->Add( PCB_ACTIONS::zoneDisplayFilled,   ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::zoneDisplayOutline,  ACTION_MENU::CHECK );

    if( ADVANCED_CFG::GetCfg().m_ExtraZoneDisplayModes )
    {
        drawingModeSubMenu->Add( PCB_ACTIONS::zoneDisplayFractured,    ACTION_MENU::CHECK );
        drawingModeSubMenu->Add( PCB_ACTIONS::zoneDisplayTriangulated, ACTION_MENU::CHECK );
    }

    drawingModeSubMenu->AppendSeparator();
    drawingModeSubMenu->Add( PCB_ACTIONS::padDisplayMode,      ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::viaDisplayMode,      ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::trackDisplayMode,    ACTION_MENU::CHECK );

    drawingModeSubMenu->AppendSeparator();
    drawingModeSubMenu->Add( PCB_ACTIONS::graphicsOutlines,    ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::textOutlines,        ACTION_MENU::CHECK );

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

#ifdef __APPLE__
    viewMenu->AppendSeparator();
#endif
}


void PCB_EDIT_FRAME::buildPlaceMenu( ACTION_MENU* placeMenu )
{
    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();

    placeMenu->Add( PCB_ACTIONS::placeFootprint );
    placeMenu->Add( PCB_ACTIONS::drawVia );
    placeMenu->Add( PCB_ACTIONS::drawZone );
    placeMenu->Add( PCB_ACTIONS::drawRuleArea );

    ACTION_MENU* muwaveSubmenu = new ACTION_MENU( false, selTool );
    muwaveSubmenu->SetTitle( _( "Draw Microwave Shapes" ) );
    muwaveSubmenu->SetIcon( BITMAPS::mw_add_line );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateLine );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateGap );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateStub );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateStubArc );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateFunctionShape );
    placeMenu->Add( muwaveSubmenu );

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
    ACTION_MENU* dimensionSubmenu = new ACTION_MENU( false, selTool );
    dimensionSubmenu->SetTitle( _( "Draw Dimensions" ) );
    dimensionSubmenu->SetIcon( BITMAPS::add_aligned_dimension );
    dimensionSubmenu->Add( PCB_ACTIONS::drawOrthogonalDimension );
    dimensionSubmenu->Add( PCB_ACTIONS::drawAlignedDimension );
    dimensionSubmenu->Add( PCB_ACTIONS::drawCenterDimension );
    dimensionSubmenu->Add( PCB_ACTIONS::drawRadialDimension );
    dimensionSubmenu->Add( PCB_ACTIONS::drawLeader );
    placeMenu->Add( dimensionSubmenu );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::placeCharacteristics );
    placeMenu->Add( PCB_ACTIONS::placeStackup );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::drillOrigin );
    placeMenu->Add( PCB_ACTIONS::drillResetOrigin );
    placeMenu->Add( ACTIONS::gridSetOrigin );
    placeMenu->Add( ACTIONS::gridResetOrigin );

    placeMenu->AppendSeparator();
    ACTION_MENU* autoplaceSubmenu = new ACTION_MENU( false, selTool );
    autoplaceSubmenu->SetTitle( _( "Auto-Place Footprints" ) );
    autoplaceSubmenu->SetIcon( BITMAPS::mode_module );

    autoplaceSubmenu->Add( PCB_ACTIONS::autoplaceOffboardComponents );
    autoplaceSubmenu->Add( PCB_ACTIONS::autoplaceSelectedComponents );

    placeMenu->Add( autoplaceSubmenu );
}


void PCB_EDIT_FRAME::buildRouteMenu( ACTION_MENU* routeMenu )
{
    routeMenu->Add( PCB_ACTIONS::selectLayerPair );

    routeMenu->AppendSeparator();
    routeMenu->Add( PCB_ACTIONS::routeSingleTrack );
    routeMenu->Add( PCB_ACTIONS::routeDiffPair );

    routeMenu->AppendSeparator();
    routeMenu->Add( PCB_ACTIONS::tuneSingleTrack );
    routeMenu->Add( PCB_ACTIONS::tuneDiffPair );
    routeMenu->Add( PCB_ACTIONS::tuneSkew );

    routeMenu->AppendSeparator();
    routeMenu->Add( PCB_ACTIONS::routerSettingsDialog );
}


void PCB_EDIT_FRAME::buildInspectMenu( ACTION_MENU* inspectMenu )
{
    // Modern layout: DRC and the marker navigation move to Tools, the diagnostics and
    // statistics to Reports, so the classic Inspect menu is dropped entirely.
    if( UseModernMenuLayout() )
        return;

    inspectMenu->Add( PCB_ACTIONS::boardStatistics );
    inspectMenu->Add( ACTIONS::measureTool );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( PCB_ACTIONS::runDRC );
    inspectMenu->Add( ACTIONS::prevMarker );
    inspectMenu->Add( ACTIONS::nextMarker );
    inspectMenu->Add( ACTIONS::excludeMarker );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( PCB_ACTIONS::inspectClearance );
    inspectMenu->Add( PCB_ACTIONS::inspectConstraints );
    inspectMenu->Add( PCB_ACTIONS::showFootprintAssociations );
    inspectMenu->Add( PCB_ACTIONS::diffFootprint );
}


void PCB_EDIT_FRAME::buildToolsMenu( ACTION_MENU* toolsMenu )
{
    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();

    if( UseModernMenuLayout() )
    {
        // Altium-style Tools: checkers, cleanup and utilities.  Cross-editor navigation lives
        // in Project, design-data operations in Design, diagnostics in Reports, and the
        // Preferences items are folded into the tail here until the title-bar gear hosts them.
        toolsMenu->Add( PCB_ACTIONS::runDRC );
        toolsMenu->Add( ACTIONS::prevMarker );
        toolsMenu->Add( ACTIONS::nextMarker );
        toolsMenu->Add( ACTIONS::excludeMarker );

        toolsMenu->AppendSeparator();
        toolsMenu->Add( ACTIONS::showFootprintEditor );
        toolsMenu->Add( PCB_ACTIONS::updateFootprints );
        toolsMenu->Add( PCB_ACTIONS::collect3DModels );

        toolsMenu->AppendSeparator();
        toolsMenu->Add( PCB_ACTIONS::cleanupTracksAndVias );
        toolsMenu->Add( PCB_ACTIONS::removeUnusedPads );
        toolsMenu->Add( PCB_ACTIONS::cleanupGraphics );
        toolsMenu->Add( PCB_ACTIONS::repairBoard );

        toolsMenu->AppendSeparator();
        toolsMenu->Add( PCB_ACTIONS::boardReannotate );
        toolsMenu->Add( ACTIONS::showCalculatorTools );

        // Anvil: External Plugins submenu removed here too (dead SWIG-plugin entries; the
        // scripting backend does not exist in this fork).

        toolsMenu->AppendSeparator();
        toolsMenu->Add( ACTIONS::configurePaths );
        toolsMenu->Add( ACTIONS::showFootprintLibTable );

        if( ADVANCED_CFG::GetCfg().m_EnablePcbDesignBlocks )
            toolsMenu->Add( ACTIONS::showDesignBlockLibTable );

        toolsMenu->Add( ACTIONS::openPreferences );

        toolsMenu->AppendSeparator();
        AddMenuLanguageList( toolsMenu, selTool );
        return;
    }

    toolsMenu->Add( ACTIONS::updatePcbFromSchematic )->Enable( !Kiface().IsSingle() );
    toolsMenu->Add( PCB_ACTIONS::showEeschema );

    if( !Kiface().IsSingle() )
        toolsMenu->Add( ACTIONS::showProjectManager );

    toolsMenu->Add( ACTIONS::showCalculatorTools );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::drcRuleEditor );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::showFootprintEditor );
    toolsMenu->Add( PCB_ACTIONS::updateFootprints );

    //Zones management
    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::zonesManager );

    if( ADVANCED_CFG::GetCfg().m_EnableGenerators )
    {
        toolsMenu->AppendSeparator();
        toolsMenu->Add( PCB_ACTIONS::generatorsShowManager );
        toolsMenu->Add( PCB_ACTIONS::regenerateAll );
        toolsMenu->Add( PCB_ACTIONS::regenerateSelected );
    }

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::cleanupTracksAndVias );
    toolsMenu->Add( PCB_ACTIONS::removeUnusedPads );
    toolsMenu->Add( PCB_ACTIONS::cleanupGraphics );
    toolsMenu->Add( PCB_ACTIONS::repairBoard );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::collect3DModels );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::boardReannotate );
    toolsMenu->Add( ACTIONS::updateSchematicFromPcb )->Enable( !Kiface().IsSingle() );

    ACTION_MENU* multichannelSubmenu = new ACTION_MENU( false, selTool );
    multichannelSubmenu->SetTitle( _( "Multi-Channel" ) );
    multichannelSubmenu->SetIcon( BITMAPS::mode_module );
    multichannelSubmenu->Add( PCB_ACTIONS::generatePlacementRuleAreas );
    multichannelSubmenu->Add( PCB_ACTIONS::repeatLayout );

    toolsMenu->Add( multichannelSubmenu );

    // Anvil: the External Plugins submenu was removed — its entries (reload / show folder)
    // belong to the SWIG action-plugin backend, which does not exist in this fork, so both
    // menu items were dead (no handler).  IPC-API plugins surface on the toolbar instead.
}


void PCB_EDIT_FRAME::buildPreferencesMenu( ACTION_MENU* prefsMenu )
{
    // Modern layout: these items live in the tail of Tools instead of a top-level menu.
    if( UseModernMenuLayout() )
        return;

    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();

    prefsMenu->Add( ACTIONS::configurePaths );
    prefsMenu->Add( ACTIONS::showFootprintLibTable );

    if( ADVANCED_CFG::GetCfg().m_EnablePcbDesignBlocks )
        prefsMenu->Add( ACTIONS::showDesignBlockLibTable );

    prefsMenu->Add( ACTIONS::openPreferences );

    prefsMenu->AppendSeparator();
    AddMenuLanguageList( prefsMenu, selTool );
}


void PCB_EDIT_FRAME::buildProjectMenu( ACTION_MENU* projectMenu )
{
    projectMenu->Add( PCB_ACTIONS::showEeschema );

    if( !Kiface().IsSingle() )
        projectMenu->Add( ACTIONS::showProjectManager );
}


void PCB_EDIT_FRAME::buildDesignMenu( ACTION_MENU* designMenu )
{
    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();

    designMenu->Add( PCB_ACTIONS::boardSetup );
    designMenu->Add( PCB_ACTIONS::drcRuleEditor );

    designMenu->AppendSeparator();
    designMenu->Add( ACTIONS::updatePcbFromSchematic )->Enable( !Kiface().IsSingle() );
    designMenu->Add( ACTIONS::updateSchematicFromPcb )->Enable( !Kiface().IsSingle() );

    designMenu->AppendSeparator();
    designMenu->Add( PCB_ACTIONS::zonesManager );
    designMenu->Add( PCB_ACTIONS::swapLayers );

    if( ADVANCED_CFG::GetCfg().m_EnableGenerators )
    {
        designMenu->AppendSeparator();
        designMenu->Add( PCB_ACTIONS::generatorsShowManager );
        designMenu->Add( PCB_ACTIONS::regenerateAll );
        designMenu->Add( PCB_ACTIONS::regenerateSelected );
    }

    designMenu->AppendSeparator();

    ACTION_MENU* multichannelSubmenu = new ACTION_MENU( false, selTool );
    multichannelSubmenu->SetTitle( _( "Multi-Channel" ) );
    multichannelSubmenu->SetIcon( BITMAPS::mode_module );
    multichannelSubmenu->Add( PCB_ACTIONS::generatePlacementRuleAreas );
    multichannelSubmenu->Add( PCB_ACTIONS::repeatLayout );
    designMenu->Add( multichannelSubmenu );
}


void PCB_EDIT_FRAME::buildReportsMenu( ACTION_MENU* reportsMenu )
{
    reportsMenu->Add( PCB_ACTIONS::boardStatistics );

    reportsMenu->AppendSeparator();
    reportsMenu->Add( PCB_ACTIONS::inspectClearance );
    reportsMenu->Add( PCB_ACTIONS::inspectConstraints );
    reportsMenu->Add( PCB_ACTIONS::showFootprintAssociations );
    reportsMenu->Add( PCB_ACTIONS::diffFootprint );

    reportsMenu->AppendSeparator();
    reportsMenu->Add( ACTIONS::measureTool );
}
