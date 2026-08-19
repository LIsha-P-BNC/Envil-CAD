/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright (C) 2018 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2009 Wayne Stambaugh <stambaughw@gmail.com>
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Copyright (C) 2019 CERN
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
#include <file_history.h>
#include <kiface_base.h>
#include <schematic.h>
#include <tool/action_manager.h>
#include <tool/action_menu.h>
#include <tool/tool_manager.h>
#include <tools/sch_selection_tool.h>
#include <tools/sch_actions.h>
#include "eeschema_id.h"
#include "sch_edit_frame.h"
#include <widgets/wx_menubar.h>
#include <widgets/panel_remote_symbol.h>
#include <advanced_config.h>


void SCH_EDIT_FRAME::doReCreateMenuBar()
{
    // Anvil Next: build the shared common menu bar instead of the legacy one when enabled.
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

    //-- File menu -----------------------------------------------------------
    //
    ACTION_MENU*   fileMenu = new ACTION_MENU( false, selTool );
    static ACTION_MENU* openRecentMenu;

    if( Kiface().IsSingle() )   // When not under a project mgr
    {
        FILE_HISTORY& fileHistory = GetFileHistory();

        // Add this menu to the list of menus managed by the file history
        // (the file history will be updated when adding/removing files in history)
        if( !openRecentMenu )
        {
            openRecentMenu = new ACTION_MENU( false, selTool );
            openRecentMenu->SetIcon( BITMAPS::recent );

            fileHistory.UseMenu( openRecentMenu );
            fileHistory.AddFilesToMenu( openRecentMenu );
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
        fileMenu->AppendSeparator();
    }

    fileMenu->Add( ACTIONS::save );

    if( Kiface().IsSingle() )
        fileMenu->Add( ACTIONS::saveAs );
    else
        fileMenu->Add( SCH_ACTIONS::saveCurrSheetCopyAs );

    fileMenu->Add( ACTIONS::revert );

    fileMenu->AppendSeparator();

    // Import submenu
    ACTION_MENU* submenuImport = new ACTION_MENU( false, selTool );
    submenuImport->SetTitle( _( "Import" ) );
    submenuImport->SetIcon( BITMAPS::import );

    submenuImport->Add( SCH_ACTIONS::importNonKicadSchematic, ACTION_MENU::NORMAL,
                        _( "Non-Anvil Schematic..." ) );
    submenuImport->Add( SCH_ACTIONS::importFPAssignments, ACTION_MENU::NORMAL, _( "Footprint Assignments..." ) );
    submenuImport->Add( SCH_ACTIONS::importGraphics, ACTION_MENU::NORMAL, _( "Graphics..." ) );

    fileMenu->Add( submenuImport );


    // Export submenu
    ACTION_MENU* submenuExport = new ACTION_MENU( false, selTool );
    submenuExport->SetTitle( _( "Export" ) );
    submenuExport->SetIcon( BITMAPS::export_file );
    submenuExport->Add( SCH_ACTIONS::drawSheetOnClipboard,   ACTION_MENU::NORMAL, _( "Drawing to Clipboard" ) );
    submenuExport->Add( SCH_ACTIONS::exportNetlist,          ACTION_MENU::NORMAL, _( "Netlist..." ) );
    submenuExport->Add( SCH_ACTIONS::exportSymbolsToLibrary, ACTION_MENU::NORMAL, _( "Symbols..." ) );
    fileMenu->Add( submenuExport );

    fileMenu->AppendSeparator();
    fileMenu->Add( SCH_ACTIONS::schematicSetup );

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::pageSettings );
    fileMenu->Add( ACTIONS::print );
    fileMenu->Add( ACTIONS::plot );

    fileMenu->AppendSeparator();
    fileMenu->AddQuitOrClose( &Kiface(), _( "Schematic Editor" ) );


    //-- Edit menu -----------------------------------------------------------
    //
    ACTION_MENU* editMenu = new ACTION_MENU( false, selTool );

    editMenu->Add( ACTIONS::undo );
    editMenu->Add( ACTIONS::redo );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut );
    editMenu->Add( ACTIONS::copy );
    editMenu->Add( ACTIONS::copyAsText );
    editMenu->Add( ACTIONS::paste );
    editMenu->Add( ACTIONS::pasteSpecial );
    editMenu->Add( ACTIONS::doDelete );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::selectAll );
    editMenu->Add( ACTIONS::unselectAll );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::find );
    editMenu->Add( ACTIONS::findAndReplace );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::deleteTool );
    editMenu->Add( SCH_ACTIONS::editTextAndGraphics );
    editMenu->Add( SCH_ACTIONS::changeSymbols );
    editMenu->Add( SCH_ACTIONS::editPageNumber );

    ACTION_MENU* submenuAttributes = new ACTION_MENU( false, selTool );
    submenuAttributes->SetTitle( _( "Attributes" ) );

    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromSim,    ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromBOM,    ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromBoard,  ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setDNP,               ACTION_MENU::CHECK );

    editMenu->Add( submenuAttributes );

    //-- View menu -----------------------------------------------------------
    //
    ACTION_MENU* viewMenu = new ACTION_MENU( false, selTool );

    // Show / Hide Panels submenu (content shared with the shell's Panels button).
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
    viewMenu->Add( ACTIONS::zoomFitObjects );
    viewMenu->Add( ACTIONS::zoomFitSelection );
    viewMenu->Add( ACTIONS::zoomTool );
    viewMenu->Add( ACTIONS::zoomRedraw );

    viewMenu->AppendSeparator();
    viewMenu->Add( SCH_ACTIONS::navigateBack );
    viewMenu->Add( SCH_ACTIONS::navigateUp );
    viewMenu->Add( SCH_ACTIONS::navigateForward );
    viewMenu->Add( SCH_ACTIONS::navigatePrevious );
    viewMenu->Add( SCH_ACTIONS::navigateNext );

    // Modern toolbar preset: the left toolbar is gone, so its display toggles surface here.
    if( ADVANCED_CFG::GetCfg().m_ModernToolbarLayout )
    {
        viewMenu->AppendSeparator();
        viewMenu->Add( ACTIONS::toggleGrid,          ACTION_MENU::CHECK );
        viewMenu->Add( ACTIONS::toggleGridOverrides, ACTION_MENU::CHECK );

        ACTION_MENU* unitsSubMenu = new ACTION_MENU( false, selTool );
        unitsSubMenu->SetTitle( _( "&Units" ) );
        unitsSubMenu->Add( ACTIONS::inchesUnits,      ACTION_MENU::CHECK );
        unitsSubMenu->Add( ACTIONS::milsUnits,        ACTION_MENU::CHECK );
        unitsSubMenu->Add( ACTIONS::millimetersUnits, ACTION_MENU::CHECK );
        viewMenu->Add( unitsSubMenu );

        ACTION_MENU* crosshairSubMenu = new ACTION_MENU( false, selTool );
        crosshairSubMenu->SetTitle( _( "&Crosshair Mode" ) );
        crosshairSubMenu->Add( ACTIONS::cursorSmallCrosshairs, ACTION_MENU::CHECK );
        crosshairSubMenu->Add( ACTIONS::cursorFullCrosshairs,  ACTION_MENU::CHECK );
        crosshairSubMenu->Add( ACTIONS::cursor45Crosshairs,    ACTION_MENU::CHECK );
        viewMenu->Add( crosshairSubMenu );

        ACTION_MENU* lineModeSubMenu = new ACTION_MENU( false, selTool );
        lineModeSubMenu->SetTitle( _( "&Line Mode" ) );
        lineModeSubMenu->Add( SCH_ACTIONS::lineModeFree, ACTION_MENU::CHECK );
        lineModeSubMenu->Add( SCH_ACTIONS::lineMode90,   ACTION_MENU::CHECK );
        lineModeSubMenu->Add( SCH_ACTIONS::lineMode45,   ACTION_MENU::CHECK );
        viewMenu->Add( lineModeSubMenu );

        viewMenu->Add( SCH_ACTIONS::toggleAnnotateAuto, ACTION_MENU::CHECK );

        if( ADVANCED_CFG::GetCfg().m_DrawBoundingBoxes )
            viewMenu->Add( ACTIONS::toggleBoundingBoxes, ACTION_MENU::CHECK );
    }

    viewMenu->AppendSeparator();
    viewMenu->Add( SCH_ACTIONS::toggleHiddenPins,      ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleHiddenFields,    ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleDirectiveLabels, ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleERCErrors,       ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleERCWarnings,     ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleERCExclusions,   ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::markSimExclusions,     ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleOPVoltages,      ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleOPCurrents,      ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::togglePinAltIcons,     ACTION_MENU::CHECK );

#ifdef __APPLE__
    viewMenu->AppendSeparator();
#endif

    //-- Place menu -----------------------------------------------------------
    //
    ACTION_MENU* placeMenu = new ACTION_MENU( false, selTool );

    placeMenu->Add( SCH_ACTIONS::placeSymbol );
    placeMenu->Add( SCH_ACTIONS::placePower );
    placeMenu->Add( SCH_ACTIONS::drawWire );
    placeMenu->Add( SCH_ACTIONS::drawBus );
    placeMenu->Add( SCH_ACTIONS::placeBusWireEntry );
    placeMenu->Add( SCH_ACTIONS::placeNoConnect );
    placeMenu->Add( SCH_ACTIONS::placeJunction );
    placeMenu->Add( SCH_ACTIONS::placeLabel );
    placeMenu->Add( SCH_ACTIONS::placeGlobalLabel );
    placeMenu->Add( SCH_ACTIONS::placeClassLabel );
    placeMenu->Add( SCH_ACTIONS::drawRuleArea );

    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::placeHierLabel );
    placeMenu->Add( SCH_ACTIONS::drawSheet );
    placeMenu->Add( SCH_ACTIONS::placeSheetPin );
    placeMenu->Add( SCH_ACTIONS::syncAllSheetsPins );
    placeMenu->Add( SCH_ACTIONS::importSheet );

    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::placeSchematicText );
    placeMenu->Add( SCH_ACTIONS::drawTextBox );
    placeMenu->Add( SCH_ACTIONS::drawTable );
    placeMenu->Add( SCH_ACTIONS::drawRectangle );
    placeMenu->Add( SCH_ACTIONS::drawCircle );
    placeMenu->Add( SCH_ACTIONS::drawEllipse );
    placeMenu->Add( SCH_ACTIONS::drawEllipseArc );
    placeMenu->Add( SCH_ACTIONS::drawArc );
    placeMenu->Add( SCH_ACTIONS::drawBezier );
    placeMenu->Add( SCH_ACTIONS::drawLines );
    placeMenu->Add( SCH_ACTIONS::placeImage );


    //-- Inspect menu -----------------------------------------------
    //
    ACTION_MENU* inspectMenu = new ACTION_MENU( false, selTool );

    inspectMenu->Add( SCH_ACTIONS::showBusSyntaxHelp );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( SCH_ACTIONS::runERC );
    inspectMenu->Add( ACTIONS::prevMarker );
    inspectMenu->Add( ACTIONS::nextMarker );
    inspectMenu->Add( ACTIONS::excludeMarker );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( SCH_ACTIONS::diffSymbol );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( SCH_ACTIONS::showSimulator );


    //-- Tools menu -----------------------------------------------
    //
    ACTION_MENU* toolsMenu = new ACTION_MENU( false, selTool );

    toolsMenu->Add( ACTIONS::updatePcbFromSchematic )->Enable( !Kiface().IsSingle() );
    toolsMenu->Add( SCH_ACTIONS::showPcbNew );

    if( !Kiface().IsSingle() )
        toolsMenu->Add( ACTIONS::showProjectManager );

    toolsMenu->Add( ACTIONS::showCalculatorTools );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::showSymbolEditor );
    toolsMenu->Add( SCH_ACTIONS::updateSymbols );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::rescueSymbols );
    toolsMenu->Add( SCH_ACTIONS::remapSymbols );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::editSymbolFields );
    toolsMenu->Add( SCH_ACTIONS::editSymbolLibraryLinks );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::annotate );
    toolsMenu->Add( SCH_ACTIONS::incrementAnnotations );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::assignFootprints );
    toolsMenu->Add( SCH_ACTIONS::generateBOM );
    toolsMenu->Add( SCH_ACTIONS::generateBOMLegacy );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::updateSchematicFromPcb )->Enable( !Kiface().IsSingle() );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::createNetChain );

    toolsMenu->AppendSeparator();
    ACTION_MENU* submenuVariants = new ACTION_MENU( false, selTool );
    submenuVariants->SetTitle( _( "Variants" ) );
    submenuVariants->Add( SCH_ACTIONS::addVariant );
    submenuVariants->Add( SCH_ACTIONS::removeVariant );
    submenuVariants->Add( SCH_ACTIONS::editVariantDescription );
    toolsMenu->Add( submenuVariants );

#ifdef KICAD_IPC_API
    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::pluginsReload );
#endif

    //-- Preferences menu -----------------------------------------------
    //
    ACTION_MENU* prefsMenu = new ACTION_MENU( false, selTool );

    prefsMenu->Add( ACTIONS::configurePaths );
    prefsMenu->Add( ACTIONS::showSymbolLibTable );
    prefsMenu->Add( ACTIONS::showDesignBlockLibTable );
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
    menuBar->Append( toolsMenu,   _( "&Tools" ) );
    menuBar->Append( prefsMenu,   _( "P&references" ) );
    AddStandardHelpMenu( menuBar );

    SetMenuBar( menuBar );
    delete oldMenuBar;
}


//================================ Anvil Next unified menu bar ================================
// The following hooks reproduce the menus built above, but populate a menu supplied by the
// shared EDA_BASE_FRAME::buildCommonMenuBar() orchestrator.  They are used only when the
// m_UnifiedMenuBar advanced flag is set; otherwise the legacy doReCreateMenuBar() above runs.

TOOL_INTERACTIVE* SCH_EDIT_FRAME::getCurrentMenuTool()
{
    return m_toolManager->GetTool<SCH_SELECTION_TOOL>();
}


void SCH_EDIT_FRAME::buildPanelsMenu( ACTION_MENU* aMenu )
{
    aMenu->Add( ACTIONS::showProperties,    ACTION_MENU::CHECK );
    aMenu->Add( ACTIONS::showSearch,        ACTION_MENU::CHECK );
    aMenu->Add( SCH_ACTIONS::showHierarchy, ACTION_MENU::CHECK );

    if( ADVANCED_CFG::GetCfg().m_IncrementalConnectivity )
        aMenu->Add( SCH_ACTIONS::showNetNavigator, ACTION_MENU::CHECK );

    // In the shell's common-AI mode the shell owns the only AI panel, so drop the per-editor
    // AI toggle (kept when CommonAiPanel is off).
    if( !( ADVANCED_CFG::GetCfg().m_SingleWindowShell
           && ADVANCED_CFG::GetCfg().m_CommonAiPanel ) )
        aMenu->Add( SCH_ACTIONS::showAiChat );

    aMenu->Add( SCH_ACTIONS::showDesignBlockPanel, ACTION_MENU::CHECK, _( "Design Blocks" ) );

    wxMenuItem* remoteSymbolItem =
            aMenu->Add( SCH_ACTIONS::showRemoteSymbolPanel, ACTION_MENU::CHECK, _( "Remote Symbols" ) );

    if( m_remoteSymbolPane && !m_remoteSymbolPane->HasDataSources() )
    {
        remoteSymbolItem->Enable( false );
        remoteSymbolItem->SetHelp( _( "Search signed-in remote symbol providers and download verified libraries." ) );
    }
}


void SCH_EDIT_FRAME::buildFileMenu( ACTION_MENU* fileMenu )
{
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();
    static ACTION_MENU* openRecentMenu;

    if( Kiface().IsSingle() )   // When not under a project mgr
    {
        FILE_HISTORY& fileHistory = GetFileHistory();

        // Add this menu to the list of menus managed by the file history
        // (the file history will be updated when adding/removing files in history)
        if( !openRecentMenu )
        {
            openRecentMenu = new ACTION_MENU( false, selTool );
            openRecentMenu->SetIcon( BITMAPS::recent );

            fileHistory.UseMenu( openRecentMenu );
            fileHistory.AddFilesToMenu( openRecentMenu );
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
        fileMenu->AppendSeparator();
    }

    fileMenu->Add( ACTIONS::save );

    if( Kiface().IsSingle() )
        fileMenu->Add( ACTIONS::saveAs );
    else
        fileMenu->Add( SCH_ACTIONS::saveCurrSheetCopyAs );

    fileMenu->Add( ACTIONS::revert );

    fileMenu->AppendSeparator();

    // Import submenu
    ACTION_MENU* submenuImport = new ACTION_MENU( false, selTool );
    submenuImport->SetTitle( _( "Import" ) );
    submenuImport->SetIcon( BITMAPS::import );

    submenuImport->Add( SCH_ACTIONS::importNonKicadSchematic, ACTION_MENU::NORMAL,
                        _( "Non-Anvil Schematic..." ) );
    submenuImport->Add( SCH_ACTIONS::importFPAssignments, ACTION_MENU::NORMAL, _( "Footprint Assignments..." ) );
    submenuImport->Add( SCH_ACTIONS::importGraphics,      ACTION_MENU::NORMAL, _( "Graphics..." ) );

    fileMenu->Add( submenuImport );


    // Export submenu
    ACTION_MENU* submenuExport = new ACTION_MENU( false, selTool );
    submenuExport->SetTitle( _( "Export" ) );
    submenuExport->SetIcon( BITMAPS::export_file );
    submenuExport->Add( SCH_ACTIONS::drawSheetOnClipboard,   ACTION_MENU::NORMAL, _( "Drawing to Clipboard" ) );
    submenuExport->Add( SCH_ACTIONS::exportNetlist,          ACTION_MENU::NORMAL, _( "Netlist..." ) );
    submenuExport->Add( SCH_ACTIONS::exportSymbolsToLibrary, ACTION_MENU::NORMAL, _( "Symbols..." ) );
    fileMenu->Add( submenuExport );

    // Modern layout: Schematic Setup lives in the Design menu instead.
    if( !UseModernMenuLayout() )
    {
        fileMenu->AppendSeparator();
        fileMenu->Add( SCH_ACTIONS::schematicSetup );
    }

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::pageSettings );
    fileMenu->Add( ACTIONS::print );
    fileMenu->Add( ACTIONS::plot );

    fileMenu->AppendSeparator();
    fileMenu->AddQuitOrClose( &Kiface(), _( "Schematic Editor" ) );
}


void SCH_EDIT_FRAME::buildEditMenu( ACTION_MENU* editMenu )
{
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();

    editMenu->Add( ACTIONS::undo );
    editMenu->Add( ACTIONS::redo );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut );
    editMenu->Add( ACTIONS::copy );
    editMenu->Add( ACTIONS::copyAsText );
    editMenu->Add( ACTIONS::paste );
    editMenu->Add( ACTIONS::pasteSpecial );
    editMenu->Add( ACTIONS::doDelete );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::selectAll );
    editMenu->Add( ACTIONS::unselectAll );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::find );
    editMenu->Add( ACTIONS::findAndReplace );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::deleteTool );
    editMenu->Add( SCH_ACTIONS::editTextAndGraphics );
    editMenu->Add( SCH_ACTIONS::changeSymbols );
    editMenu->Add( SCH_ACTIONS::editPageNumber );

    ACTION_MENU* submenuAttributes = new ACTION_MENU( false, selTool );
    submenuAttributes->SetTitle( _( "Attributes" ) );

    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromSim,    ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromBOM,    ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromBoard,  ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setDNP,               ACTION_MENU::CHECK );

    editMenu->Add( submenuAttributes );
}


void SCH_EDIT_FRAME::buildViewMenu( ACTION_MENU* viewMenu )
{
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();

    // Show / Hide Panels submenu (content shared with the shell's Panels button).
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
    viewMenu->Add( ACTIONS::zoomFitObjects );
    viewMenu->Add( ACTIONS::zoomFitSelection );
    viewMenu->Add( ACTIONS::zoomTool );
    viewMenu->Add( ACTIONS::zoomRedraw );

    viewMenu->AppendSeparator();
    viewMenu->Add( SCH_ACTIONS::navigateBack );
    viewMenu->Add( SCH_ACTIONS::navigateUp );
    viewMenu->Add( SCH_ACTIONS::navigateForward );
    viewMenu->Add( SCH_ACTIONS::navigatePrevious );
    viewMenu->Add( SCH_ACTIONS::navigateNext );

    // Modern toolbar preset: the left toolbar is gone, so its display toggles surface here.
    if( ADVANCED_CFG::GetCfg().m_ModernToolbarLayout )
    {
        viewMenu->AppendSeparator();
        viewMenu->Add( ACTIONS::toggleGrid,          ACTION_MENU::CHECK );
        viewMenu->Add( ACTIONS::toggleGridOverrides, ACTION_MENU::CHECK );

        ACTION_MENU* unitsSubMenu = new ACTION_MENU( false, selTool );
        unitsSubMenu->SetTitle( _( "&Units" ) );
        unitsSubMenu->Add( ACTIONS::inchesUnits,      ACTION_MENU::CHECK );
        unitsSubMenu->Add( ACTIONS::milsUnits,        ACTION_MENU::CHECK );
        unitsSubMenu->Add( ACTIONS::millimetersUnits, ACTION_MENU::CHECK );
        viewMenu->Add( unitsSubMenu );

        ACTION_MENU* crosshairSubMenu = new ACTION_MENU( false, selTool );
        crosshairSubMenu->SetTitle( _( "&Crosshair Mode" ) );
        crosshairSubMenu->Add( ACTIONS::cursorSmallCrosshairs, ACTION_MENU::CHECK );
        crosshairSubMenu->Add( ACTIONS::cursorFullCrosshairs,  ACTION_MENU::CHECK );
        crosshairSubMenu->Add( ACTIONS::cursor45Crosshairs,    ACTION_MENU::CHECK );
        viewMenu->Add( crosshairSubMenu );

        ACTION_MENU* lineModeSubMenu = new ACTION_MENU( false, selTool );
        lineModeSubMenu->SetTitle( _( "&Line Mode" ) );
        lineModeSubMenu->Add( SCH_ACTIONS::lineModeFree, ACTION_MENU::CHECK );
        lineModeSubMenu->Add( SCH_ACTIONS::lineMode90,   ACTION_MENU::CHECK );
        lineModeSubMenu->Add( SCH_ACTIONS::lineMode45,   ACTION_MENU::CHECK );
        viewMenu->Add( lineModeSubMenu );

        viewMenu->Add( SCH_ACTIONS::toggleAnnotateAuto, ACTION_MENU::CHECK );

        if( ADVANCED_CFG::GetCfg().m_DrawBoundingBoxes )
            viewMenu->Add( ACTIONS::toggleBoundingBoxes, ACTION_MENU::CHECK );
    }

    viewMenu->AppendSeparator();
    viewMenu->Add( SCH_ACTIONS::toggleHiddenPins,      ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleHiddenFields,    ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleDirectiveLabels, ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleERCErrors,       ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleERCWarnings,     ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleERCExclusions,   ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::markSimExclusions,     ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleOPVoltages,      ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleOPCurrents,      ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::togglePinAltIcons,     ACTION_MENU::CHECK );

#ifdef __APPLE__
    viewMenu->AppendSeparator();
#endif
}


void SCH_EDIT_FRAME::buildPlaceMenu( ACTION_MENU* placeMenu )
{
    placeMenu->Add( SCH_ACTIONS::placeSymbol );
    placeMenu->Add( SCH_ACTIONS::placePower );
    placeMenu->Add( SCH_ACTIONS::drawWire );
    placeMenu->Add( SCH_ACTIONS::drawBus );
    placeMenu->Add( SCH_ACTIONS::placeBusWireEntry );
    placeMenu->Add( SCH_ACTIONS::placeNoConnect );
    placeMenu->Add( SCH_ACTIONS::placeJunction );
    placeMenu->Add( SCH_ACTIONS::placeLabel );
    placeMenu->Add( SCH_ACTIONS::placeGlobalLabel );
    placeMenu->Add( SCH_ACTIONS::placeClassLabel );
    placeMenu->Add( SCH_ACTIONS::drawRuleArea );

    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::placeHierLabel );
    placeMenu->Add( SCH_ACTIONS::drawSheet );
    placeMenu->Add( SCH_ACTIONS::placeSheetPin );
    placeMenu->Add( SCH_ACTIONS::syncAllSheetsPins );
    placeMenu->Add( SCH_ACTIONS::importSheet );

    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::placeSchematicText );
    placeMenu->Add( SCH_ACTIONS::drawTextBox );
    placeMenu->Add( SCH_ACTIONS::drawTable );
    placeMenu->Add( SCH_ACTIONS::drawRectangle );
    placeMenu->Add( SCH_ACTIONS::drawCircle );
    placeMenu->Add( SCH_ACTIONS::drawArc );
    placeMenu->Add( SCH_ACTIONS::drawBezier );
    placeMenu->Add( SCH_ACTIONS::drawLines );
    placeMenu->Add( SCH_ACTIONS::placeImage );
}


void SCH_EDIT_FRAME::buildInspectMenu( ACTION_MENU* inspectMenu )
{
    // Modern layout: ERC, the simulator and the bus syntax help move to Tools and the library
    // diff to Reports, so the classic Inspect menu is dropped entirely.
    if( UseModernMenuLayout() )
        return;

    inspectMenu->Add( SCH_ACTIONS::showBusSyntaxHelp );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( SCH_ACTIONS::runERC );
    inspectMenu->Add( ACTIONS::prevMarker );
    inspectMenu->Add( ACTIONS::nextMarker );
    inspectMenu->Add( ACTIONS::excludeMarker );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( SCH_ACTIONS::diffSymbol );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( SCH_ACTIONS::showSimulator );
}


void SCH_EDIT_FRAME::buildToolsMenu( ACTION_MENU* toolsMenu )
{
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();

    if( UseModernMenuLayout() )
    {
        // Altium-style Tools: checkers and utilities.  Cross-editor navigation lives in
        // Project, design-data operations in Design, generated listings in Reports, and the
        // Preferences items are folded into the tail here until the title-bar gear hosts them.
        toolsMenu->Add( SCH_ACTIONS::runERC );
        toolsMenu->Add( ACTIONS::prevMarker );
        toolsMenu->Add( ACTIONS::nextMarker );
        toolsMenu->Add( ACTIONS::excludeMarker );

        toolsMenu->AppendSeparator();
        toolsMenu->Add( SCH_ACTIONS::annotate );
        toolsMenu->Add( SCH_ACTIONS::incrementAnnotations );
        toolsMenu->Add( SCH_ACTIONS::assignFootprints );

        toolsMenu->AppendSeparator();
        toolsMenu->Add( SCH_ACTIONS::showSimulator );
        toolsMenu->Add( SCH_ACTIONS::showBusSyntaxHelp );

        toolsMenu->AppendSeparator();
        toolsMenu->Add( ACTIONS::showSymbolEditor );
        toolsMenu->Add( SCH_ACTIONS::updateSymbols );
        toolsMenu->Add( SCH_ACTIONS::rescueSymbols );
        toolsMenu->Add( SCH_ACTIONS::remapSymbols );

        toolsMenu->AppendSeparator();
        toolsMenu->Add( SCH_ACTIONS::editSymbolFields );
        toolsMenu->Add( SCH_ACTIONS::editSymbolLibraryLinks );

        toolsMenu->AppendSeparator();
        toolsMenu->Add( ACTIONS::showCalculatorTools );

#ifdef KICAD_IPC_API
        toolsMenu->AppendSeparator();
        toolsMenu->Add( ACTIONS::pluginsReload );
#endif

        toolsMenu->AppendSeparator();
        toolsMenu->Add( ACTIONS::configurePaths );
        toolsMenu->Add( ACTIONS::showSymbolLibTable );
        toolsMenu->Add( ACTIONS::showDesignBlockLibTable );
        toolsMenu->Add( ACTIONS::openPreferences );

        toolsMenu->AppendSeparator();
        AddMenuLanguageList( toolsMenu, selTool );
        return;
    }

    toolsMenu->Add( ACTIONS::updatePcbFromSchematic )->Enable( !Kiface().IsSingle() );
    toolsMenu->Add( SCH_ACTIONS::showPcbNew );

    if( !Kiface().IsSingle() )
        toolsMenu->Add( ACTIONS::showProjectManager );

    toolsMenu->Add( ACTIONS::showCalculatorTools );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::showSymbolEditor );
    toolsMenu->Add( SCH_ACTIONS::updateSymbols );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::rescueSymbols );
    toolsMenu->Add( SCH_ACTIONS::remapSymbols );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::editSymbolFields );
    toolsMenu->Add( SCH_ACTIONS::editSymbolLibraryLinks );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::annotate );
    toolsMenu->Add( SCH_ACTIONS::incrementAnnotations );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::assignFootprints );
    toolsMenu->Add( SCH_ACTIONS::generateBOM );
    toolsMenu->Add( SCH_ACTIONS::generateBOMLegacy );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::updateSchematicFromPcb )->Enable( !Kiface().IsSingle() );

    toolsMenu->AppendSeparator();
    ACTION_MENU* submenuVariants = new ACTION_MENU( false, selTool );
    submenuVariants->SetTitle( _( "Variants" ) );
    submenuVariants->Add( SCH_ACTIONS::addVariant );
    submenuVariants->Add( SCH_ACTIONS::removeVariant );
    toolsMenu->Add( submenuVariants );

#ifdef KICAD_IPC_API
    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::pluginsReload );
#endif
}


void SCH_EDIT_FRAME::buildPreferencesMenu( ACTION_MENU* prefsMenu )
{
    // Modern layout: these items live in the tail of Tools instead of a top-level menu.
    if( UseModernMenuLayout() )
        return;

    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();

    prefsMenu->Add( ACTIONS::configurePaths );
    prefsMenu->Add( ACTIONS::showSymbolLibTable );
    prefsMenu->Add( ACTIONS::showDesignBlockLibTable );
    prefsMenu->Add( ACTIONS::openPreferences );

    prefsMenu->AppendSeparator();
    AddMenuLanguageList( prefsMenu, selTool );
}


void SCH_EDIT_FRAME::buildProjectMenu( ACTION_MENU* projectMenu )
{
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();

    projectMenu->Add( SCH_ACTIONS::showPcbNew );

    if( !Kiface().IsSingle() )
        projectMenu->Add( ACTIONS::showProjectManager );

    // Altium runs schematic checking from Project → Compile/Validate; ERC also stays under
    // Tools next to its marker navigation — both habits work, nothing is lost.
    projectMenu->AppendSeparator();
    projectMenu->Add( SCH_ACTIONS::runERC, ACTION_MENU::NORMAL, _( "Validate Project (ERC)" ) );

    projectMenu->AppendSeparator();

    ACTION_MENU* submenuVariants = new ACTION_MENU( false, selTool );
    submenuVariants->SetTitle( _( "Variants" ) );
    submenuVariants->Add( SCH_ACTIONS::addVariant );
    submenuVariants->Add( SCH_ACTIONS::removeVariant );
    projectMenu->Add( submenuVariants );
}


void SCH_EDIT_FRAME::buildDesignMenu( ACTION_MENU* designMenu )
{
    // Annotation and footprint assignment live under Tools (matching Altium's Tools →
    // Annotation / Footprint Manager); Design keeps the design-data operations.
    designMenu->Add( SCH_ACTIONS::schematicSetup );

    designMenu->AppendSeparator();
    designMenu->Add( ACTIONS::updatePcbFromSchematic )->Enable( !Kiface().IsSingle() );
    designMenu->Add( ACTIONS::updateSchematicFromPcb )->Enable( !Kiface().IsSingle() );
}


void SCH_EDIT_FRAME::buildReportsMenu( ACTION_MENU* reportsMenu )
{
    reportsMenu->Add( SCH_ACTIONS::generateBOM );
    reportsMenu->Add( SCH_ACTIONS::generateBOMLegacy );

    reportsMenu->AppendSeparator();
    reportsMenu->Add( SCH_ACTIONS::diffSymbol );
}
