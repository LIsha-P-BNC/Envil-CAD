/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright (C) 2013 CERN (www.cern.ch)
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

#ifndef KICAD_H
#define KICAD_H

#include <kiway_player.h>
#include <kiway.h>           // KIFACE_TAB_HOST (single-window shell dock bridge)

#include <utility>
#include <vector>
#include <wx/timer.h>

class ACTION_TOOLBAR;
class BITMAP_BUTTON;
class EDA_BASE_FRAME;
class KICAD_SETTINGS;
class PANEL_KICAD_LAUNCHER;
class PLUGIN_CONTENT_MANAGER;
class PROJECT_TREE;
class PROJECT_TREE_PANE;
class LOCAL_HISTORY_PANE;
class UPDATE_MANAGER;
class WEBVIEW_PANEL;
class AI_IPC_CLIENT;
class ANVIL_AI_AGENT;
class ANVIL_AI_TOOL_SERVER;

/**
 * The main Anvil project manager frame.  It is not a KIWAY_PLAYER.
 *
 * It also implements KIFACE_TAB_HOST so editor KIFACEs can dock newly opened sibling
 * editors as tabs in the single-window shell (see KIWAY::DockPlayer).
 */
class KICAD_MANAGER_FRAME : public EDA_BASE_FRAME, public KIFACE_TAB_HOST
{
public:
    KICAD_MANAGER_FRAME( wxWindow* parent, const wxString& title,
                         const wxPoint& pos, const wxSize& size );

    ~KICAD_MANAGER_FRAME();

    void OnIdle( wxIdleEvent& event );

    bool canCloseWindow( wxCloseEvent& aCloseEvent ) override;
    void doCloseWindow() override;
    void OnSize( wxSizeEvent& event ) override;

    void UnarchiveFiles();
    void RestoreLocalHistory();
    void RestoreCommitFromHistory( const wxString& aHash );
    void ToggleLocalHistory();
    bool HistoryPanelShown();

    /// Single-window shell: show/hide the left Project Explorer pane (driven by the
    /// VS Code-style layout toggle in the custom title bar).
    void ToggleProjectExplorer();
    bool ProjectExplorerShown();

    /// Single-window shell: split the editor-tab area into side-by-side groups, moving the
    /// active editor into the new group (VS Code "split editor"; needs 2+ open editors).
    void SplitActiveEditor();

    /// Single-window shell common AI panel (CommonAiPanel): the shell owns ONE "AI Assistant"
    /// pane (Cursor style) instead of one per editor.  Name of that pane, and a show/hide
    /// toggle + visibility query driven by the title-bar AI button.  No-op when the panel was
    /// not created (flag off, or WebView init failed).
    static const wxString AiChatPanelName() { return wxT( "AiChat" ); }
    void ToggleAiChat();
    bool AiChatPanelShown();

    /// Single-window shell: arrange the AI panel as a side-by-side editor split (Cursor
    /// "open AI beside code") — show it docked right at ~40% width so it sits next to the
    /// active editor rather than as the narrow sidebar.  Driven by the title-bar AI-split
    /// button.  No-op when the AI panel was not created.
    void ShowAiSplitLayout();

    void OnOpenFileInTextEditor( wxCommandEvent& event );
    void OnEditAdvancedCfg( wxCommandEvent& event );
    void OnAnvilSignOut( wxCommandEvent& event );

    /// Anvil "Vibrant Purple & Indigo" theme: repaint the shell's own chrome (AUI dock area,
    /// editor/side tab strips, project tree, launcher, status bar) to match the frame theme.
    /// Gated by the AnvilPurpleFrame advanced-config flag; does NOT descend into the hosted
    /// editor tabs (those keep their own theme + canvas).
    void applyAnvilShellTheme();

    void OnFileHistory( wxCommandEvent& event );
    void OnClearFileHistory( wxCommandEvent& aEvent );
    void OnExit( wxCommandEvent& event );

    /** Create the status line (like a wxStatusBar). This is actually a KISTATUSBAR status bar.
     * the specified number of fields is the extra number of fields, not the full field count.
     * @return a KISTATUSBAR (derived from wxStatusBar)
     */
    wxStatusBar* OnCreateStatusBar( int number, long style, wxWindowID id,
                                    const wxString& name ) override;

    /**
     * Hides the tabs for Editor notebook if there is only 1 page
     */
    void HideTabsIfNeeded();

    /**
     * Anvil Next single-window shell (Layer B): re-host an in-process editor frame
     * (Schematic / PCB / Gerber / Calculator / …) as a tab in the manager window's
     * center editor notebook instead of letting it float as its own OS window.
     * The frame's top-level decorations are stripped so the shell's single title /
     * menu bar owns the chrome — the editor itself is unchanged.
     *
     * Only active when ADVANCED_CFG m_SingleWindowShell is set (otherwise m_editorTabs
     * is null and this is a no-op returning false, so the caller floats the frame as
     * before). Windows-only for now; returns false elsewhere.
     *
     * @return true if the frame was docked (or its existing tab was selected).
     */
    bool DockEditorAsTab( KIWAY_PLAYER* aPlayer, const wxString& aTitle );

    /**
     * KIFACE_TAB_HOST implementation: cross-KIFACE entry point letting eeschema / pcbnew
     * dock a sibling editor they just opened (e.g. "Update PCB", "Update Schematic") as a
     * tab in this shell.  Delegates to DockEditorAsTab() using the player's own title.
     */
    bool DockPlayerAsTab( KIWAY_PLAYER* aPlayer ) override;

    /**
     * KIFACE_TAB_HOST implementation: report whether @a aPlayer is currently hosted as a
     * live tab.  Used by editor KIFACEs to avoid re-opening (reverting) an editor that is
     * already loaded but parked on a background tab.
     */
    bool IsPlayerDocked( KIWAY_PLAYER* aPlayer ) override;

    /**
     * Remove docked editor tabs whose KIWAY_PLAYER frame has been destroyed (e.g. on
     * project close).  Validity is tested by window-id via wxWindow::FindWindowById —
     * the same mechanism KIWAY itself uses — so it never dereferences a freed frame and
     * is safe to call at any time the shell is alive.  No-op when the shell is off.
     */
    void PruneDeadEditorTabs();

    /**
     * Detach a docked editor frame from its tab host (reverse of DockEditorAsTab): restore
     * its top-level window decorations and hide it WITHOUT destroying it, so KIWAY's player
     * pointer stays valid and the editor can be re-docked later.  Windows-only; no-op
     * elsewhere.  Called when the user clicks a tab's close (X) button.
     */
    void DetachDockedEditor( wxWindow* aPlayer );

    wxString GetCurrentFileName() const override;

    /**
     * @brief Creates a project and imports a non-Anvil Schematic and PCB
     * @param aWindowTitle to display to the user when opening the files
     * @param aFilesWildcard that includes both PCB and Schematic files (from
     * wildcards_and_files_ext.h)
     * @param aSchFileExtensions e.g. { "sch" } or { "csa" }. Specify { "INPUT" } to copy input file.
     * @param aPcbFileExtensions e.g. { "brd" } or { "cpa" }. Specify { "INPUT" } to copy input file.
     * @param aSchFileType Type of Schematic File to import (from SCH_IO_MGR::SCH_FILE_T)
     * @param aPcbFileType Type of PCB File to import (from IO_MGR::PCB_FILE_T)
    */
    void ImportNonKiCadProject( const wxString& aWindowTitle, const wxString& aFilesWildcard,
                                const std::vector<std::string>& aSchFileExtensions,
                                const std::vector<std::string>& aPcbFileExtensions,
                                int aSchFileType, int aPcbFileType );

    /**
     * Open dialog to import Altium project files.
     */
    /// Convert a Anvil project into an Anvil project (copy, rename to .anvil_*, rewrite
    /// hierarchical sheet references, open the result).
    /**
     * Convert a project's kicad_pro/kicad_sch/kicad_pcb files to the Anvil extensions,
     * rewriting quoted internal references. Used by the Import menu, the open-time
     * conversion offer, and the non-Anvil importers (Altium etc.).
     *
     * @param aKeepOriginals true = leave the Anvil files in place (convert-as-copy).
     */
    bool ConvertProjectToAnvil( const wxFileName& aSrcPro, const wxString& aDestDir,
                                bool aKeepOriginals, wxFileName* aDestProOut = nullptr );

    /// Convert a Anvil/Altium symbol library to .anvil_sym via eeschema (KIWAY mail).
    /// After an import, offer to review/clean the design with Anvil AI. Pre-fills the AI
    /// panel's composer (never auto-sends, so the user decides when a turn starts).
    void OfferAiImportCleanup( const wxString& aWhat );

    void ImportSymbolLibrary();

    /// Convert a footprint library to an Anvil footprint library via pcbnew (KIWAY mail).
    void ImportFootprintLibrary();

    void OnImportKiCadProject( wxCommandEvent& event );

    /// Altium-style unified import: one dialog, all supported formats, output = Anvil project.
    void OnImportProject( wxCommandEvent& event );

private:
    void importKiCadProjectFile( const wxString& aInputPath );

    /// Direct-open router: a foreign (Anvil or legacy) project reached LoadProject via
    /// File>Open, CLI, double-click, MRU or drag-drop.  Offers to import & convert it —
    /// foreign projects are never opened natively.  Returns true when an import ran.
    bool OfferImportForeignProject( const wxFileName& aProjectFile );

    void importProjectFromFile( const wxString& aInputPath,
                                const std::vector<std::string>& aSchFileExtensions,
                                const std::vector<std::string>& aPcbFileExtensions,
                                int aSchFileType, int aPcbFileType );

public:

    void OnImportAltiumProjectFiles( wxCommandEvent& event );

    /**
     *  Open dialog to import CADSTAR Schematic and PCB Archive files.
     */
    void OnImportCadstarArchiveFiles( wxCommandEvent& event );

    /**
     *  Open dialog to import Eagle schematic and board files.
     */
    void OnImportEagleFiles( wxCommandEvent& event );

    /**
     *  Open dialog to import EasyEDA Std schematic and board files.
     */
    void OnImportEasyEdaFiles( wxCommandEvent& event );

    /**
     *  Open dialog to import EasyEDA Pro schematic and board files.
     */
    void OnImportEasyEdaProFiles( wxCommandEvent& event );

    /**
     *  Open dialog to import PADS Logic schematic and PCB files.
     */
    void OnImportPadsProjectFiles( wxCommandEvent& event );

    /**
     *  Open dialog to import gEDA/gaf schematic and PCB files.
     */
    void OnImportGedaFiles( wxCommandEvent& event );

    /**
     * Prints the current working directory name and the project name on the text panel.
     */
    void PrintPrjInfo();

    void RefreshProjectTree();

    /**
     * Creates a new project by setting up and initial project, schematic, and board files.
     *
     * The project file is copied from the kicad.pro template file if possible.  Otherwise,
     * a minimal project file is created from an empty project.  A minimal schematic and
     * board file are created to prevent the schematic and board editors from complaining.
     * If any of these files already exist, they are not overwritten.
     *
     * @param aProjectFileName is the absolute path of the project file name.
     * @param aCreateStubFiles specifies if an empty PCB and schematic should be created
     */
    void CreateNewProject( const wxFileName& aProjectFileName, bool aCreateStubFiles = true );

    /**
     * Closes the project, and saves it if aSave is true;
     */
    bool CloseProject( bool aSave );

    /**
     * Loads a new project
     * @param aProjectFileName is the path to the project to load
     * @return true if the project was successfully loaded
     */
    bool LoadProject( const wxFileName& aProjectFileName );

    /**
     * Open a file inside THIS already-running shell window instead of spawning a second
     * process/window (VS Code / Cursor style).  Called for the initial command-line file and
     * for files handed over from a later launch via the single-instance IPC bridge (see
     * kicad.cpp).
     *
     * @a aPath is classified by extension using FILEEXT (the single source of the
     * extension→type mapping):
     *   - project file    → LoadProject()
     *   - schematic/board → load its sibling project (if not already active) and open the
     *                       matching editor as a tab (via the editSchematic / editPCB action,
     *                       which routes through KICAD_MANAGER_CONTROL::ShowPlayer →
     *                       DockEditorAsTab).
     * An empty @a aPath just raises/focuses the shell (a bare second launch).  The window is
     * always brought to the front so opening a file behaves like focusing the app.
     */
    void OpenAnvilFile( const wxString& aPath );

    /**
     * Entry point for a file handed over from a *second* launch via the single-instance IPC
     * bridge (kicad.cpp).  Always raises this window, then:
     *   - a file of the currently-open project (same directory) → open it here as a tab
     *     (delegates to OpenAnvilFile());
     *   - a file of a DIFFERENT project → launch a fresh instance ("--new <path>") so it gets
     *     its own window, instead of silently swapping the active project (and its unsaved
     *     edits) out from under the user.  Same rule as VS Code opening a different folder.
     * Runs on the GUI thread (the IPC callback marshals here via CallAfter).
     */
    void HandleForwardedOpen( const wxString& aPath );

    void OpenJobsFile( const wxFileName& aFileName, bool aCreate = false,
                       bool aResaveProjectPreferences = true );


    void LoadSettings( APP_SETTINGS_BASE* aCfg ) override;

    void SaveSettings( APP_SETTINGS_BASE* aCfg ) override;

    void ShowChangedLanguage() override;
    void CommonSettingsChanged( int aFlags ) override;
    void ProjectChanged() override;

    void PreloadAllLibraries();

    /**
     * Called by sending a event with id = ID_INIT_WATCHED_PATHS
     * rebuild the list of watched paths
     */
    void OnChangeWatchedPaths( wxCommandEvent& aEvent );

    const wxString GetProjectFileName() const;

    bool IsProjectActive();
    // read only accessors
    const wxString SchFileName();
    const wxString SchLegacyFileName();
    const wxString PcbFileName();
    const wxString PcbLegacyFileName();

    void ReCreateTreePrj();

    /**
     * @param aIsExplicitUserSave is true to indicate the user ran a Save Project action explicitly
     *        Note that this parameter should currently *always* be false, because there is no
     *        explicit Save Project action in the project manager.  This means that anytime the
     *        project manager saves project local settings, it is an implicit save (and should not
     *        actually save the file if it was migrated)
     */
    void SaveOpenJobSetsToLocalSettings( bool aIsExplicitUserSave = false );

    wxWindow* GetToolCanvas() const override;

    std::shared_ptr<PLUGIN_CONTENT_MANAGER> GetPcm() { return m_pcm; };

    void SetPcmButton( BITMAP_BUTTON* aButton );

    void CreatePCM();   // creates the PLUGIN_CONTENT_MANAGER

    // Used only on Windows: stores the info message about file watcher
    wxString m_FileWatcherInfo;

    DECLARE_EVENT_TABLE()

protected:
    virtual void setupUIConditions() override;

    void doReCreateMenuBar() override;

    // Anvil Next unified menu bar (see EDA_BASE_FRAME::buildCommonMenuBar()).
    TOOL_INTERACTIVE* getCurrentMenuTool() override;
    void buildFileMenu( ACTION_MENU* aMenu ) override;
    void buildEditMenu( ACTION_MENU* aMenu ) override;
    void buildViewMenu( ACTION_MENU* aMenu ) override;
    void buildToolsMenu( ACTION_MENU* aMenu ) override;
    void buildProjectMenu( ACTION_MENU* aMenu ) override;
    void buildPreferencesMenu( ACTION_MENU* aMenu ) override;
    void buildPanelsMenu( ACTION_MENU* aMenu ) override;

public:
    /**
     * Anvil Next: titlebar quick access (0=Save, 1=Undo, 2=Redo) and the Preferences gear
     * (3), dispatched to the ACTIVE editor tab's tool manager (resolved per click).
     */
    void RunQuickAccessAction( int aWhich );

    /// Dim/brighten the quick-access buttons from the active editor's real state.
    void RefreshQuickAccess();

    /// Update the Altium-style document/project name shown in the title bar.
    void RefreshShellDocumentTitle();

    /// Pop the Altium "Open editor" dropdown (replaces the removed left icon rail).
    void ShowOpenEditorMenu( const wxPoint& aScreenPos );
    void buildOpenEditorMenu( ACTION_MENU* aMenu );

protected:

    void onToolbarSizeChanged();

    void onNotebookPageCloseRequest( wxAuiNotebookEvent& evt );

    void onNotebookPageCountChanged( wxAuiNotebookEvent& evt );

    /// Single-window shell: user clicked a center editor tab's close (X) button.  Detaches
    /// the editor frame (kept alive, re-dockable) and lets the empty host page be destroyed.
    void onEditorTabCloseRequest( wxAuiNotebookEvent& evt );

    /// Single-window shell: the active center editor tab changed (user switched tabs); make
    /// the top (title-bar) menu follow it.  See syncShellMenuToActiveTab().
    void onEditorTabChanged( wxAuiNotebookEvent& evt );

    /// Single-window shell + unified menu: rebuild the shell's top menu from the editor in the
    /// active center tab (Schematic/PCB/… contribute their own File/Edit/View/Place/Route/
    /// Inspect/Tools menus), falling back to the Project Manager's own menu when no editor tab
    /// is active.  No-op unless both shell flags are set.
    void syncShellMenuToActiveTab( bool aForcePM = false );

    /// Single-window shell: a non-editor pane (the Project Explorer tree) gained focus, so the
    /// user is on the Project Manager — restore its own menu even while editor tabs stay open.
    void onShellPaneFocus( wxChildFocusEvent& aEvent );

    /// Single-window shell: focus returned to the center editor-tab area — show the active
    /// editor's menu again.
    void onEditorAreaFocus( wxChildFocusEvent& aEvent );

#ifdef __WXMSW__
    /// Custom single-row title bar: intercept WM_NCCALCSIZE/NCHITTEST/GETMINMAXINFO so the
    /// native caption is removed and replaced by the app-drawn title strip (VS Code style).
    WXLRESULT MSWWindowProc( WXUINT message, WXWPARAM wParam, WXLPARAM lParam ) override;
#endif

    /// (Re)populate the custom title bar's menu buttons from the live menu bar.
    void buildTitleBarMenuButtons();

private:
    void setupTools();
    void setupActions();

    /// Single-window shell: the editor frame hosted in the currently-selected center tab, or
    /// null when none is selected.  Resolved by matching the active page against m_dockedEditors
    /// and looking the player up by window-id (same validity test as PruneDeadEditorTabs()).
    EDA_BASE_FRAME* getActiveDockedEditorFrame();

    /// Single-window shell common AI panel: push the active editor tab's document (app +
    /// path) into the shell-owned AI panel via window.anvilSetSchematic / anvilSetPcb, so
    /// the one panel always targets whatever tab is in front (the Cursor behaviour).  No-op
    /// when the panel does not exist or the active tab is neither schematic nor PCB.
    void syncAiPanelToActiveTab();

    /// Single-window shell unified footer (m_UnifiedStatusBar): each docked editor shows its OWN
    /// native status bar (exactly like standalone Anvil); this just hides the shell's own status
    /// bar while an editor tab is in front (so there is only one footer) and shows it again on the
    /// Project Manager tab. No-op unless SingleWindowShell + UnifiedStatusBar are both set.
    void syncShellStatusBarToActiveTab();

    /// UnifiedToolbar: move aEditor's top toolbar (Standard + aux row) OUT of its tab and dock
    /// it in a shell strip ABOVE the editor tab bar, so the layout matches Altium
    /// (Title -> Menu -> Toolbar -> Tabs -> Workspace).  Reparent-safe: ACTION_TOOLBAR dispatches
    /// through a stored TOOL_MANAGER pointer, so its buttons keep driving the editor after the
    /// widget is reparented.  No-op unless SingleWindowShell + UnifiedToolbar, or already hoisted.
    void hoistEditorTopToolbar( EDA_BASE_FRAME* aEditor );

    /// Reverse hoistEditorTopToolbar(): return aEditor's toolbars to its own AUI so a standalone
    /// (undocked) editor keeps its toolbar.  Called before the WS_CHILD reversal in
    /// DetachDockedEditor(), and for every editor at shell teardown.
    void restoreEditorTopToolbar( EDA_BASE_FRAME* aEditor );

    /// Show the active editor tab's hoisted toolbar strip and hide every other editor's, so only
    /// the front tab's toolbar is visible above the tabs.
    void syncShellToolbarToActiveTab();

    /// Single-window shell: queue the heavy editor KIFACEs (Symbol/Footprint/Gerber/
    /// Drawing-Sheet) to be warmed in the background after startup so the user's first
    /// click on one is instant instead of "loading the whole app".  Gated on
    /// m_SingleWindowShell + m_ShellPrewarmEditors; a no-op otherwise.
    void schedulePrewarmEditors();

    /// Timer handler: create (but do not show) the next queued editor on the GUI thread,
    /// then re-arm for the one after it.  Strictly one-at-a-time, never concurrent, so it
    /// cannot race a foreground editor open.
    void prewarmNextEditor( wxTimerEvent& aEvent );

    void DoWithAcceptedFiles() override;

    APP_SETTINGS_BASE* config() const override;

    KICAD_SETTINGS* kicadSettings() const;

    const SEARCH_STACK& sys_search() override;

    wxString help_name() override;

    void updatePcmButtonBadge();

private:
    bool                  m_openSavedWindows;
    bool                  m_restoredFromHistory;  ///< Set after restore to mark editors dirty
    int                   m_leftWinWidth;
    bool                  m_active_project;
    bool                  m_showHistoryPanel;

    PROJECT_TREE_PANE*    m_projectTreePane;
    LOCAL_HISTORY_PANE*   m_historyPane;
    wxAuiNotebook*        m_notebook;
    wxAuiNotebook*        m_editorTabs;   ///< Center editor-tab area; only created when m_SingleWindowShell

    /// UnifiedToolbar: editors whose top toolbars have been hoisted above the tab bar.  Each entry
    /// owns the editor's window-id plus its (reparented-to-shell) main + aux ACTION_TOOLBARs, so
    /// syncShellToolbarToActiveTab() can toggle visibility and DetachDockedEditor() can restore them.
    struct HOISTED_EDITOR_TOOLBAR
    {
        int             editorId;
        ACTION_TOOLBAR* main;
        ACTION_TOOLBAR* aux;
        ACTION_TOOLBAR* activeBar; ///< Altium-style Active Bar, hoisted as a 3rd top row (Layer 4)
        ACTION_TOOLBAR* left;    ///< drawing-tools rail, flipped horizontal + hoisted to the top
        ACTION_TOOLBAR* right;   ///< second drawing rail (if the editor uses one), same treatment
    };
    std::vector<HOISTED_EDITOR_TOOLBAR> m_hoistedToolbars;
    WEBVIEW_PANEL*        m_aiChatPanel;  ///< Shell-owned common AI panel; only when CommonAiPanel + shell
    ANVIL_AI_AGENT*       m_anvilAgent;   ///< Native Claude agent driving m_aiChatPanel
    std::unique_ptr<ANVIL_AI_TOOL_SERVER> m_anvilToolServer;  ///< MCP tool socket (loopback)

    /// Single-window shell: true while the title-bar menu shows an editor's menu (vs the
    /// Project Manager's own menu).  Lets focus changes flip the menu without rebuilding it
    /// on every event.  See onShellPaneFocus()/onEditorAreaFocus().
    bool                  m_shellMenuShowsEditor = false;

    /// Re-entrancy guard for syncShellMenuToActiveTab().  Rebuilding the title-bar menu
    /// destroys the buttons and the menus behind them, and the resulting focus changes fire
    /// wxEVT_CHILD_FOCUS -- which calls straight back in and frees those same menus a second
    /// time.  See syncShellMenuToActiveTab().
    bool                  m_syncingShellMenu = false;

    /// Docked editors as (player window-id, host page) pairs.  Window-id (not pointer)
    /// so a destroyed player is detected via FindWindowById without a dangling deref.
    std::vector<std::pair<int, wxWindow*>> m_dockedEditors;

    /// Single-window shell editor pre-warm: FRAME_T values (stored as int to keep this
    /// header light) still to warm, and the timer that warms the next one after a short
    /// idle gap.  Empty and stopped once warming is complete.
    std::vector<int>      m_prewarmQueue;
    wxTimer               m_prewarmTimer;

    PANEL_KICAD_LAUNCHER* m_launcher;
    int                   m_lastToolbarIconSize;

    std::shared_ptr<PLUGIN_CONTENT_MANAGER> m_pcm;
    BITMAP_BUTTON*                          m_pcmButton;
    int                                     m_pcmUpdateCount;
    std::unique_ptr<UPDATE_MANAGER>         m_updateManager;

    // Anvil Next custom single-row title bar (logo + menu + window buttons)
    class TITLEBAR_PANEL;          // defined in kicad_manager_frame.cpp
    TITLEBAR_PANEL*                         m_titleBar = nullptr;

    // Anvil Next (Cursor-style): the SHELL's own backend command channel. The editors
    // listen for open_file/revert; the shell listens for "open_project" and calls
    // LoadProject() so a just-built project appears in the Project Files tree and updates
    // live (LoadProject rebuilds the tree + resets the file watcher). Created only when the
    // shell AI panel exists; the backend emits open_project behind a config flag, so this is
    // purely additive. Port is resolved lazily (retry timer) like the editor IPC clients.
    std::unique_ptr<AI_IPC_CLIENT>          m_aiIpcClient;
    wxTimer                                 m_aiIpcRetryTimer;
    int                                     m_aiIpcRetryAttempts = 0;

    /// Resolve the backend IPC port (reads ipc_port.txt, same search order as the editors)
    /// and (re)connect m_aiIpcClient. Returns true on success. Mirrors SCH_EDIT_FRAME.
    bool TryConnectAiIpc();

    /// Retry-timer tick: keep re-attempting the IPC connect (backoff) until the backend is up.
    void OnAiIpcRetryTimer( wxTimerEvent& aEvent );
};


// The C++ project manager includes a single PROJECT in its link image.
class PROJECT;
extern PROJECT& Prj();

#endif
