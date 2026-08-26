/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright (C) 2004-2015 Jean-Pierre Charras, jp.charras at wanadoo.fr
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
 * @file kicad.cpp
 * Main Anvil project manager file.
 */


#include <wx/filename.h>
#include <wx/log.h>
#include <wx/app.h>
#include <wx/stdpaths.h>
#include <wx/msgdlg.h>
#include <wx/cmdline.h>
#include <wx/ipc.h>
#ifndef __WXMSW__
#include <wx/socket.h>   // non-Windows IPC rides TCP; used for the pre-connect timeout probe
#endif
#include <wx/snglinst.h>

#include <functional>
#include <memory>

#include <common.h>
#include <env_vars.h>
#include <file_history.h>
#include <hotkeys_basic.h>
#include <kiway.h>
#include <macros.h>
#include <paths.h>
#include <anvil_auth/anvil_auth.h>
#include <dialogs/dialog_anvil_login.h>
#include <richio.h>
#include <settings/settings_manager.h>
#include <settings/kicad_settings.h>
#include <settings/common_settings.h>
#include <../include/startwizard/startwizard.h>
#include <systemdirsappend.h>
#include <thread_pool.h>
#include <trace_helpers.h>
#include <wildcards_and_files_ext.h>
#include <confirm.h>

#include <git/git_backend.h>
#include <git/libgit_backend.h>
#include <stdexcept>

#include "pgm_kicad.h"
#include "kicad_manager_frame.h"

#include <kiplatform/app.h>
#include <kiplatform/anvil_theme.h>
#include <kiplatform/environment.h>
#include <widgets/ui_common.h>
#include <advanced_config.h>

#ifdef KICAD_IPC_API
#include <api/api_server.h>
#endif

// a dummy to quiet linking with EDA_BASE_FRAME::config();
#include <kiface_base.h>

#include <libraries/library_manager.h>


KIFACE_BASE& Kiface()
{
    // This function should never be called.  It is only referenced from
    // EDA_BASE_FRAME::config() and this is only provided to satisfy the linker,
    // not to be actually called.
    wxLogFatalError( wxT( "Unexpected call to Kiface() in kicad/kicad.cpp" ) );

    throw std::logic_error( "Unexpected call to Kiface() in kicad/kicad.cpp" );
}


static PGM_KICAD program;

PGM_KICAD& PgmTop()
{
    return program;
}


#if wxUSE_IPC
// ---------------------------------------------------------------------------------------------
// Single-window shell: hand a file from a *second* launch to the ALREADY-running Anvil so it
// opens as a tab, instead of cold-starting another heavyweight process/window (the real cause
// of the "new window every time / slow" behaviour).  VS Code / Cursor style.
//
// Transport is wxWidgets IPC (DDE on Windows) — no extra dependency, and self-contained to this
// binary.  The channel name is derived from the running app name (not a hard-coded product
// string) so it cannot collide with other Anvil-family executables and needs no config.
// ---------------------------------------------------------------------------------------------

static const wxString ANVIL_IPC_TOPIC = wxS( "open" );

#ifdef __WXMSW__
static wxString anvilInstanceService()
{
    // Windows transport is DDE, whose service is a free-form name.  Both the launching (client)
    // and running (server) instance are the same executable, so wxApp::GetAppName() (the exe
    // basename, e.g. "anvilcad") is identical in both and needs no hard-coded product string.
    return wxTheApp->GetAppName() + wxS( "-open-ipc" );
}
#else
static wxString anvilInstanceService()
{
    // Non-Windows wxIPC rides TCP, whose service must be a numeric port.  Derive a stable port
    // from the app name (the same input the DDE path keys off) so distinct Anvil-family
    // executables get distinct ports with no hard-coded product string or config.  FNV-1a, folded
    // into the IANA dynamic/private range (49152-65535).  Server bind and client connect both call
    // this, so they always agree on the port.
    wxUint32 h = 2166136261u;

    for( wxUniChar c : wxTheApp->GetAppName() )
        h = ( h ^ ( c.GetValue() & 0xFFu ) ) * 16777619u;

    return wxString::Format( wxS( "%u" ), 49152u + ( h % 16384u ) );
}
#endif

// Set on the first (server) instance; invoked when a later launch forwards a path.  Empty
// payload means "just raise the window".
static std::function<void( const wxString& )> g_onForwardedOpen;

class ANVIL_IPC_CONNECTION : public wxConnection
{
public:
    bool OnExec( const wxString& WXUNUSED( aTopic ), const wxString& aData ) override
    {
        if( g_onForwardedOpen )
            g_onForwardedOpen( aData );

        return true;
    }
};

class ANVIL_IPC_SERVER : public wxServer
{
public:
    wxConnectionBase* OnAcceptConnection( const wxString& aTopic ) override
    {
        return aTopic == ANVIL_IPC_TOPIC ? new ANVIL_IPC_CONNECTION : nullptr;
    }
};

class ANVIL_IPC_CLIENT : public wxClient
{
public:
    wxConnectionBase* OnMakeConnection() override { return new ANVIL_IPC_CONNECTION; }
};

static std::unique_ptr<ANVIL_IPC_SERVER> g_instanceServer;

// Become the single-window IPC listener if no instance currently is.  Called at startup and
// again whenever a shell window is activated: if the instance that owned the listener is closed,
// the next activated window re-registers the service, so Explorer opens keep docking into a
// running window instead of silently cold-starting from then on (self-healing listener election).
static void anvilEnsureInstanceListener()
{
    if( g_instanceServer )
        return;   // this instance already hosts the listener

    auto server = std::make_unique<ANVIL_IPC_SERVER>();

    if( server->Create( anvilInstanceService() ) )
        g_instanceServer = std::move( server );
    // else: another instance owns the service — remain a non-listener for now.
}

// Hand @a aPayload (an absolute file path, or empty to just raise) to a running Anvil instance.
// Returns true if a running instance accepted it, false if none could be reached (caller then
// opens its own window, so behaviour is unchanged when nothing is listening).
static bool anvilForwardToRunningInstance( const wxString& aPayload )
{
    wxLogNull         suppressConnectErrors;   // no popup when there is nothing to connect to

#ifndef __WXMSW__
    // Non-Windows wxServer/wxClient become wxTCPServer/wxTCPClient.  On Windows this whole block
    // is absent: DDE fails fast on a dead service, so MakeConnection() below is already safe.  On
    // TCP a dead-but-bound listener can otherwise block MakeConnection() indefinitely, so probe
    // the port with an explicit timeout first.  No answer within the timeout is treated exactly
    // like "no instance running": return false so the caller opens its own window (behaviour
    // identical to a failed connect).
    //
    // Caveat: this catches an unreachable/refused port.  A stale listener that accepts the TCP
    // connection but never completes the IPC handshake would still stall MakeConnection(); fully
    // covering that needs an on-target (Linux/macOS) test to size a handshake timeout, which
    // can't be exercised on the Windows build.
    {
        long port = 0;
        anvilInstanceService().ToLong( &port );

        wxIPV4address addr;
        addr.Hostname( wxS( "localhost" ) );
        addr.Service( static_cast<unsigned short>( port ) );

        wxSocketClient probe;
        probe.SetTimeout( 2 );                       // seconds; expiry => safe fallback

        if( !probe.Connect( addr, true /* wait */ ) )
            return false;

        probe.Close();
    }
#endif

    ANVIL_IPC_CLIENT  client;

    wxConnectionBase* conn = client.MakeConnection( wxS( "localhost" ), anvilInstanceService(),
                                                    ANVIL_IPC_TOPIC );

    if( !conn )
        return false;

    bool ok = conn->Execute( aPayload );
    conn->Disconnect();

    return ok;
}
#endif // wxUSE_IPC


bool PGM_KICAD::OnPgmInit()
{
    App().SetAppDisplayName( wxT( "Anvil" ) );

#if defined(DEBUG)
    wxString absoluteArgv0 = wxStandardPaths::Get().GetExecutablePath();

    if( !wxIsAbsolutePath( absoluteArgv0 ) )
    {
        wxLogError( wxT( "No meaningful argv[0]" ) );
        return false;
    }
#endif

    // Initialize the git backend before trying to initialize individual programs
    SetGitBackend( new LIBGIT_BACKEND() );
    GetGitBackend()->Init();

    static const wxCmdLineEntryDesc desc[] = {
        { wxCMD_LINE_OPTION, "f", "frame", "Frame to load", wxCMD_LINE_VAL_STRING, 0 },
        { wxCMD_LINE_SWITCH, "n", "new", "New instance of Anvil, does not attempt to load previously open files",
          wxCMD_LINE_VAL_NONE, 0 },
#ifndef __WXOSX__
        { wxCMD_LINE_SWITCH, nullptr, "software-rendering", "Use software rendering instead of OpenGL",
          wxCMD_LINE_VAL_NONE, 0 },
#endif
        { wxCMD_LINE_PARAM, nullptr, nullptr, "File to load", wxCMD_LINE_VAL_STRING,
          wxCMD_LINE_PARAM_MULTIPLE | wxCMD_LINE_PARAM_OPTIONAL },
        { wxCMD_LINE_NONE, nullptr, nullptr, nullptr, wxCMD_LINE_VAL_NONE, 0 }
    };

    wxCmdLineParser parser( App().argc, App().argv );
    parser.SetDesc( desc );
    parser.Parse( false );

    FRAME_T appType = KICAD_MAIN_FRAME_T;

    const struct
    {
        wxString name;
        FRAME_T  type;
    } frameTypes[] = { { wxT( "pcb" ), FRAME_PCB_EDITOR },
                       { wxT( "fpedit" ), FRAME_FOOTPRINT_EDITOR },
                       { wxT( "sch" ), FRAME_SCH },
                       { wxT( "calc" ), FRAME_CALC },
                       { wxT( "bm2cmp" ), FRAME_BM2CMP },
                       { wxT( "ds" ), FRAME_PL_EDITOR },
                       { wxT( "gerb" ), FRAME_GERBER },
                       { wxT( "" ), FRAME_T_COUNT } };

    wxString frameName;

    if( parser.Found( "frame", &frameName ) )
    {
        appType = FRAME_T_COUNT;

        for( const auto& it : frameTypes )
        {
            if( it.name == frameName )
                appType = it.type;
        }

        if( appType == FRAME_T_COUNT )
        {
            wxLogError( wxT( "Unknown frame: %s" ), frameName );
            // Clean up
            OnPgmExit();
            return false;
        }
    }

    if( appType == KICAD_MAIN_FRAME_T )
    {
        Kiway.SetCtlBits( KFCTL_CPP_PROJECT_SUITE );
    }
    else
    {
        Kiway.SetCtlBits( KFCTL_STANDALONE );
    }

#ifndef __WXMAC__
    if( parser.Found( "software-rendering" ) )
    {
        wxSetEnv( "KICAD_SOFTWARE_RENDERING", "1" );
    }
#endif

    if( !InitPgm( false ) )
        return false;

#if wxUSE_IPC
    // Single-window shell: if another Anvil is already running, hand our file(s) to it and
    // exit — so opening a file from Explorer (or any second launch) reuses the one window
    // instead of cold-starting a second heavyweight process.  Additive & safe: with no running
    // instance, or if the handoff fails, we fall through and start normally.  Only the project
    // manager shell participates; an explicit "--frame ..." standalone editor, or "-n/--new",
    // is left to open its own window on purpose.
    if( appType == KICAD_MAIN_FRAME_T && !parser.FoundSwitch( "new" )
        && SingleInstance()->IsAnotherRunning() )
    {
        wxString payload;   // absolute path of the first file arg; empty => just raise the window

        if( parser.GetParamCount() > 0 )
        {
            wxFileName argFn( parser.GetParam( 0 ) );
            argFn.MakeAbsolute();
            payload = argFn.GetFullPath();
        }

        if( anvilForwardToRunningInstance( payload ) )
            return false;   // handed off; APP_KICAD::OnInit() runs OnPgmExit() for cleanup
    }
#endif


    m_bm.InitSettings( new KICAD_SETTINGS );
    GetSettingsManager().RegisterSettings( PgmSettings() );
    GetSettingsManager().SetKiway( &Kiway );
    m_bm.Init();

    // Anvil emerald frame theme.  Point every palette copy this module can reach at the
    // persisted theme (COMMON_SETTINGS appearance.app_theme) BEFORE a single window exists, so
    // the title bar, menu row and tool-bars are built in the right colours from the first paint.
    const bool anvilPurpleFrame = ADVANCED_CFG::GetCfg().m_AnvilPurpleFrame;

    KIUI::SyncAnvilTheme();

    // NEMI Emerald LIGHT: leave wx's MSW dark mode alone.  wxMSW offers only DarkMode_Auto and
    // DarkMode_Always (there is no DarkMode_Never) and dark mode is opt-in, so simply not
    // enabling it is what gives genuinely light native controls, menus and scrollbars.  The
    // Anvil chrome (title bar, menu band, tool-bars, panels) paints itself from the palette
    // either way.
    if( !ANVIL::IsLight() )
    {
        if( anvilPurpleFrame )
            KIPLATFORM::APP::SetDarkModePurple( true );

        if( const COMMON_SETTINGS* cfg = Pgm().GetCommonSettings() )
        {
            if( anvilPurpleFrame || cfg->m_Appearance.app_theme == APP_THEME::DARK )
                KIPLATFORM::APP::EnableDarkMode( true );
            else if( cfg->m_Appearance.app_theme == APP_THEME::AUTO )
                KIPLATFORM::APP::EnableDarkMode( false );
        }
    }

    // Add search paths to feed the PGM_KICAD::SysSearch() function,
    // currently limited in support to only look for project templates
    {
        SEARCH_STACK bases;

        SystemDirsAppend( &bases );

        for( unsigned i = 0; i < bases.GetCount(); ++i )
        {
            wxFileName fn( bases[i], wxEmptyString );

            // Add Anvil template file path to search path list.
            fn.AppendDir( wxT( "template" ) );

            // Only add path if exists and can be read by the user.
            if( fn.DirExists() && fn.IsDirReadable() )
                m_bm.m_search.AddPaths( fn.GetPath() );
        }

        auto insertExpanded = [&]( const wxString& aValue )
        {
            wxString resolved = ExpandEnvVarSubstitutions( aValue, nullptr );

            // Skip values that still contain unresolved variable references so we don't
            // pollute the search stack with paths like "${MISSING}/templates".
            if( resolved.Contains( wxT( "${" ) ) || resolved.Contains( wxT( "$(" ) ) )
                return;

            m_bm.m_search.Insert( resolved, 0 );
        };

        // The versioned TEMPLATE_DIR takes precedence over the search stack template path.
        if( std::optional<wxString> v = ENV_VAR::GetVersionedEnvVarValue( GetLocalEnvVariables(),
                                                                          wxT( "TEMPLATE_DIR" ) ) )
        {
            if( !v->IsEmpty() )
                insertExpanded( *v );
        }

        // We've been adding system (installed default) search paths so far, now for user paths
        // The default user search path is inside KIPLATFORM::ENV::GetDocumentsPath()
        m_bm.m_search.Insert( PATHS::GetUserTemplatesPath(), 0 );

        // ...but the user can override that default with the KICAD_USER_TEMPLATE_DIR env var.
        // The value may itself reference other KiCad path variables, so expand them here.
        ENV_VAR_MAP_CITER it = GetLocalEnvVariables().find( "KICAD_USER_TEMPLATE_DIR" );

        if( it != GetLocalEnvVariables().end() && it->second.GetValue() != wxEmptyString )
            insertExpanded( it->second.GetValue() );
    }

    // Anvil sign-in gate.  The dialog is deliberately NOT destroyed here: it stays on screen
    // showing "opening your workspace" while the manager frame is built, and is torn down
    // only once that window is visible (see below).  Destroying it first would leave the
    // desktop bare for the whole of startup, which reads as the app having restarted.
    //
    // While it is the only top-level window, wxWidgets' "last window closed => quit" rule
    // must also be suspended, or its destruction would end the app.
    std::unique_ptr<DIALOG_ANVIL_LOGIN> loginDlg;
    const bool                          exitOnDelete = App().GetExitOnFrameDelete();

    if( !ANVIL_AUTH::IsLoggedIn() )
    {
        App().SetExitOnFrameDelete( false );

        loginDlg = std::make_unique<DIALOG_ANVIL_LOGIN>( nullptr );

        if( loginDlg->ShowModal() != wxID_OK )
        {
            loginDlg.reset();
            App().SetExitOnFrameDelete( exitOnDelete );
            OnPgmExit();
            return false;
        }

        // ShowModal() hid the dialog; bring it back as a plain window so it covers the
        // screen for the rest of startup.
        //
        // Order matters.  The window has to be up, and at its final maximized size, BEFORE
        // ShowOpeningState() paints it: a repaint asked for while it is hidden is dropped, and
        // the very next thing this thread does is block for seconds building the main window,
        // with no event loop left to service one later.  Painting last is what stops the cover
        // standing there full of whatever the desktop had under it.
        loginDlg->Show( true );
        loginDlg->Raise();
        loginDlg->ShowOpeningState();
    }

    // Advance the cover's loading rail as each real startup step clears.  Harmless when there
    // is no cover (an already-signed-in launch), so the call sites need no guard of their own.
    auto openingStep =
            [&loginDlg]( double aFraction, const wxString& aStep )
            {
                if( loginDlg )
                    loginDlg->SetOpeningProgress( aFraction, aStep );
            };

    wxFrame*      frame = nullptr;
    KIWAY_PLAYER* playerFrame = nullptr;
    KICAD_MANAGER_FRAME* managerFrame = nullptr;

    if( appType == KICAD_MAIN_FRAME_T )
    {
        openingStep( 0.18, _( "Building your workspace…" ) );

        managerFrame = new KICAD_MANAGER_FRAME( nullptr, wxT( "Anvil" ), wxDefaultPosition,
                                                wxWindow::FromDIP( wxSize( 775, -1 ), NULL ) );
        frame = managerFrame;

        openingStep( 0.55, _( "Setting up the editors…" ) );

        STARTWIZARD startWizard;
        startWizard.CheckAndRun( frame );
    }
    else
    {
        // Use KIWAY to create a top window, which registers its existence also.
        // "TOP_FRAME" is a macro that is passed on compiler command line from CMake,
        // and is one of the types in FRAME_T.
        playerFrame = Kiway.Player( appType, true );
        frame = playerFrame;

        if( frame == nullptr )
        {
            return false;
        }
    }

    App().SetTopWindow( frame );

    if( playerFrame )
        App().SetAppDisplayName( playerFrame->GetAboutTitle() );

    Kiway.SetTop( frame );

    KIPLATFORM::ENV::SetAppDetailsForWindow( frame, '"' + wxStandardPaths::Get().GetExecutablePath() + '"' + " -n",
                                             frame->GetTitle() );

    KICAD_SETTINGS* settings = static_cast<KICAD_SETTINGS*>( PgmSettings() );

    openingStep( 0.70, _( "Loading the component libraries…" ) );

    GetLibraryManager().LoadGlobalTables();

#ifdef KICAD_IPC_API
    m_api_server = std::make_unique<KICAD_API_SERVER>();
    m_api_common_handler = std::make_unique<API_HANDLER_COMMON>();
    m_api_server->RegisterHandler( m_api_common_handler.get() );
#endif

    wxString projToLoad;

    HideSplash();

    if( playerFrame && parser.GetParamCount() )
    {
        // Now after the frame processing, the rest of the positional args are files
        std::vector<wxString> fileArgs;
        /*
            gerbview handles multiple project data files, i.e. gerber files on
            cmd line. Others currently do not, they handle only one. For common
            code simplicity we simply pass all the arguments in however, each
            program module can do with them what they want, ignore, complain
            whatever.  We don't establish policy here, as this is a multi-purpose
            launcher.
        */

        for( size_t i = 0; i < parser.GetParamCount(); i++ )
            fileArgs.push_back( parser.GetParam( i ) );

        // special attention to a single argument: argv[1] (==argSet[0])
        if( fileArgs.size() == 1 )
        {
            wxFileName argv1( fileArgs[0] );

#if defined( PGM_DATA_FILE_EXT )
            // PGM_DATA_FILE_EXT, if present, may be different for each compile,
            // it may come from CMake on the compiler command line, but often does not.
            // This facility is mostly useful for those program footprints
            // supporting a single argv[1].
            if( !argv1.GetExt() )
                argv1.SetExt( wxT( PGM_DATA_FILE_EXT ) );
#endif
            argv1.MakeAbsolute();

            fileArgs[0] = argv1.GetFullPath();
        }

        // Use the KIWAY_PLAYER::OpenProjectFiles() API function:
        if( !playerFrame->OpenProjectFiles( fileArgs ) )
        {
            // OpenProjectFiles() API asks that it report failure to the UI.
            // Nothing further to say here.

            // We've already initialized things at this point, but wx won't call OnExit if
            // we fail out. Call our own cleanup routine here to ensure the relevant resources
            // are freed at the right time (if they aren't, segfaults will occur).
            OnPgmExit();

            // Fail the process startup if the file could not be opened,
            // although this is an optional choice, one that can be reversed
            // also in the KIFACE specific OpenProjectFiles() return value.
            return false;
        }
    }
    else if( managerFrame )
    {
        bool deferredEditorFile = false;   // a non-project file arg opened via OpenAnvilFile below

        if( parser.GetParamCount() > 0 )
        {
            wxFileName tmp = parser.GetParam( 0 );

            if( tmp.GetExt() != FILEEXT::AnvilProjectFileExtension
                && tmp.GetExt() != FILEEXT::ProjectFileExtension
                && tmp.GetExt() != FILEEXT::LegacyProjectFileExtension )
            {
                // Not a project file (e.g. a .anvil_sch / .anvil_pcb opened from Explorer):
                // open it inside this shell as an editor tab once the window is visible, rather
                // than rejecting it.  OpenAnvilFile() classifies it and loads its own project.
                tmp.MakeAbsolute();
                wxString editorFile = tmp.GetFullPath();
                deferredEditorFile  = true;
                managerFrame->CallAfter( [managerFrame, editorFile]()
                                         {
                                             managerFrame->OpenAnvilFile( editorFile );
                                         } );
            }
            else
            {
                projToLoad = tmp.GetFullPath();
            }
        }

        // If no project was given as an argument, re-open the last one — but not when we are
        // about to open a specific editor file (deferredEditorFile): OpenAnvilFile() will load
        // that file's own project, so auto-loading the previous project first would just flash
        // the wrong project and then switch.
        if( projToLoad.IsEmpty() && !deferredEditorFile && settings->m_OpenProjects.size()
            && !parser.FoundSwitch( "new" ) )
        {
            wxString last_pro = settings->m_OpenProjects.front();
            settings->m_OpenProjects.erase( settings->m_OpenProjects.begin() );

            if( wxFileExists( last_pro ) )
            {
                // Try to open the last opened project,
                // if a project name is not given when starting Kicad
                projToLoad = last_pro;
            }
        }

        bool loaded = false;

        // Do not attempt to load a non-existent project file.
        if( !projToLoad.empty() )
        {
            wxFileName fn( projToLoad );

            if( fn.Exists() && (   fn.GetExt() == FILEEXT::AnvilProjectFileExtension
                                || fn.GetExt() == FILEEXT::ProjectFileExtension
                                || fn.GetExt() == FILEEXT::LegacyProjectFileExtension ) )
            {
                fn.MakeAbsolute();

                if( appType == KICAD_MAIN_FRAME_T )
                {
                    if( fn.GetExt() == FILEEXT::AnvilProjectFileExtension )
                    {
                        loaded = managerFrame->LoadProject( fn );
                    }
                    else
                    {
                        // A foreign (Anvil/legacy) argument routes to a modal import offer
                        // inside LoadProject; we're still before frame->Show(), so defer it
                        // until the frame is visible.
                        managerFrame->CallAfter( [managerFrame, fn]()
                                                 {
                                                     managerFrame->LoadProject( fn );
                                                 } );
                    }
                }
            }
        }

        if( !loaded && appType == KICAD_MAIN_FRAME_T )
            managerFrame->PreloadAllLibraries();
    }

    openingStep( 0.92, _( "Almost ready…" ) );

    frame->Show( true );

    // The workspace is up: retire the sign-in cover and restore the normal shutdown rule.
    if( loginDlg )
    {
        loginDlg->Destroy();
        loginDlg.release();     // wxWidgets owns it after Destroy()
        App().SetExitOnFrameDelete( exitOnDelete );
    }
    frame->Raise();

#if wxUSE_IPC
    // First (and only) instance: start the local IPC listener so later launches can hand us a
    // file to open as a tab (see anvilForwardToRunningInstance()).  Only the manager shell hosts
    // tabs, so only it listens.
    if( managerFrame )
    {
        g_onForwardedOpen =
                [managerFrame]( const wxString& aPath )
                {
                    // We are inside an IPC (DDE) callback here — marshal onto the GUI event loop
                    // (correct on Windows DDE, and stays correct if the transport ever becomes
                    // wxTCPServer, whose callbacks are not guaranteed on the main thread).
                    managerFrame->CallAfter( [managerFrame, aPath]()
                                             {
                                                 managerFrame->HandleForwardedOpen( aPath );
                                             } );
                };

        // Try to become the listener now, and re-try on activation so listener ownership
        // self-heals if the instance that currently owns it is closed (see
        // anvilEnsureInstanceListener).
        anvilEnsureInstanceListener();

        managerFrame->Bind( wxEVT_ACTIVATE,
                            []( wxActivateEvent& evt )
                            {
                                if( evt.GetActive() )
                                    anvilEnsureInstanceListener();

                                evt.Skip();
                            } );
    }
#endif

#ifdef KICAD_IPC_API
    m_api_server->SetReadyToReply();
#endif

    return true;
}


int PGM_KICAD::OnPgmRun()
{
    return 0;
}


void PGM_KICAD::OnPgmExit()
{
#if wxUSE_IPC
    // Stop the single-window IPC listener before teardown so no forwarded-open callback fires
    // into a half-destroyed frame.
    g_onForwardedOpen = nullptr;
    g_instanceServer.reset();
#endif
    // Signal all background library preloads to abort before waiting for the thread pool.
    // The design block preload runs on the global thread pool and checks this flag; without
    // setting it here the pool wait below can block for up to 120 seconds.
    m_libraryPreloadAbort.store( true );

    // Abort and wait on any background jobs
    GetKiCadThreadPool().purge();
    GetKiCadThreadPool().wait();

    Kiway.OnKiwayEnd();

#ifdef KICAD_IPC_API
    m_api_server.reset();
#endif

    if( m_settings_manager && m_settings_manager->IsOK() )
    {
        SaveCommonSettings();
        m_settings_manager->Save();
    }

    // Destroy everything in PGM_KICAD,
    // especially wxSingleInstanceCheckerImpl earlier than wxApp and earlier
    // than static destruction would.
    Destroy();
    GetGitBackend()->Shutdown();
    delete GetGitBackend();
    SetGitBackend( nullptr );
}


void PGM_KICAD::MacOpenFile( const wxString& aFileName )
{
#if defined(__WXMAC__)

    KICAD_MANAGER_FRAME* frame = (KICAD_MANAGER_FRAME*) App().GetTopWindow();

    if( !aFileName.empty() && wxFileExists( aFileName ) )
        frame->LoadProject( wxFileName( aFileName ) );

#endif
}


void PGM_KICAD::Destroy()
{
    // unlike a normal destructor, this is designed to be called more
    // than once safely:

    m_bm.End();

    PGM_BASE::Destroy();
}


KIWAY  Kiway( KFCTL_CPP_PROJECT_SUITE );

#ifdef NDEBUG
// Define a custom assertion handler
void CustomAssertHandler(const wxString& file,
                         int line,
                         const wxString& func,
                         const wxString& cond,
                         const wxString& msg)
{
    Pgm().HandleAssert( file, line, func, cond, msg );
}
#endif

/**
 * Not publicly visible because most of the action is in #PGM_KICAD these days.
 */
struct APP_KICAD : public wxApp
{
    APP_KICAD() : wxApp()
    {
        SetPgm( &program );

        // Init the environment each platform wants
        KIPLATFORM::ENV::Init();
    }


    bool OnInit()           override
    {
#ifdef NDEBUG
        // These checks generate extra assert noise
        wxSizerFlags::DisableConsistencyChecks();
        wxDISABLE_DEBUG_SUPPORT();
        wxSetAssertHandler( CustomAssertHandler );
#endif

        // Perform platform-specific init tasks
        if( !KIPLATFORM::APP::Init() )
            return false;

#ifndef DEBUG
        // Enable logging traces to the console in release build.
        // This is usually disabled, but it can be useful for users to run to help
        // debug issues and other problems.
        if( wxGetEnv( wxS( "KICAD_ENABLE_WXTRACE" ), nullptr ) )
        {
            wxLog::EnableLogging( true );
            wxLog::SetLogLevel( wxLOG_Trace );
        }
#endif

        if( !program.OnPgmInit() )
        {
            program.OnPgmExit();
            return false;
        }

        return true;
    }

    int OnExit() override
    {
        // Drain wxPendingDelete (frames deferred via Destroy()) before tearing down
        // PGM_BASE singletons. On macOS the dock-quit path leaves frames in this
        // queue at OnExit() time, and their canvas destructors call into
        // Pgm().GetGLContextManager(). Running OnPgmExit() first would null that
        // pointer out from under them. See https://gitlab.com/kicad/code/kicad/-/issues/23373
        int ret = wxApp::OnExit();

        // Avoid wxLog crashing when used in destructors invoked from OnPgmExit().
        wxLog::EnableLogging( false );

        program.OnPgmExit();
        return ret;
    }


    int OnRun() override
    {
        try
        {
            return wxApp::OnRun();
        }
        catch(...)
        {
            Pgm().HandleException( std::current_exception() );
        }

        return -1;
    }


    void OnUnhandledException() override
    {
        Pgm().HandleException( std::current_exception(), true );
    }


    int FilterEvent( wxEvent& aEvent ) override
    {
        if( aEvent.GetEventType() == wxEVT_SHOW )
        {
            wxShowEvent& event = static_cast<wxShowEvent&>( aEvent );
            wxDialog*    dialog = dynamic_cast<wxDialog*>( event.GetEventObject() );

            std::vector<void*>& dlgs = Pgm().m_ModalDialogs;

            if( dialog )
            {
                if( event.IsShown() && dialog->IsModal() )
                {
                    dlgs.push_back( dialog );
                }
                // Under GTK, sometimes the modal flag is cleared before hiding
                else if( !event.IsShown() && !dlgs.empty() )
                {
                    // If we close the expected dialog, remove it from our stack
                    if( dlgs.back() == dialog )
                        dlgs.pop_back();
                    // If an out-of-order, remove all dialogs added after the closed one
                    else if( auto it = std::find( dlgs.begin(), dlgs.end(), dialog ) ; it != dlgs.end() )
                        dlgs.erase( it, dlgs.end() );
                }
            }
        }

        return Event_Skip;
    }

#if defined( DEBUG )
    /**
     * Process any unhandled events at the application level.
     */
    bool ProcessEvent( wxEvent& aEvent ) override
    {
        if( aEvent.GetEventType() == wxEVT_CHAR || aEvent.GetEventType() == wxEVT_CHAR_HOOK )
        {
            wxKeyEvent* keyEvent = static_cast<wxKeyEvent*>( &aEvent );

            if( keyEvent )
            {
                wxLogTrace( kicadTraceKeyEvent, "APP_KICAD::ProcessEvent %s", dump( *keyEvent ) );
            }
        }

        aEvent.Skip();
        return false;
    }

    /**
     * Override main loop exception handling on debug builds.
     *
     * It can be painfully difficult to debug exceptions that happen in wxUpdateUIEvent
     * handlers.  The override provides a bit more useful information about the exception
     * and a breakpoint can be set to pin point the event where the exception was thrown.
     */
    bool OnExceptionInMainLoop() override
    {
        try
        {
            throw;
        }
        catch(...)
        {
            Pgm().HandleException( std::current_exception() );
        }

        return false;   // continue on. Return false to abort program
    }
#endif

    /**
     * Set MacOS file associations.
     *
     * @see http://wiki.wxwidgets.org/WxMac-specific_topics
     */
#if defined( __WXMAC__ )
    void MacOpenFile( const wxString& aFileName ) override
    {
        Pgm().MacOpenFile( aFileName );
    }
#endif
};

IMPLEMENT_APP( APP_KICAD )


// The C++ project manager supports one open PROJECT, so Prj() calls within
// this link image need this function.
PROJECT& Prj()
{
    return Kiway.Prj();
}

