/*
 * Anvil AI — native chat agent. See anvil_ai_agent.h.
 */

#include <anvil_ai/anvil_ai_agent.h>

#include <cstring>
#include <thread>

#include <wx/dir.h>
#include <wx/translation.h>
#include <wx/ffile.h>
#include <wx/filefn.h>          // wxFileExists
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>           // wxGetEnv

#include <paths.h>
#include <settings/settings_manager.h>
#include <widgets/webview_panel.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using json = nlohmann::json;


// One MCP server (the SKiDL server) carries every tool, so one server-level rule allows
// them all — generation and live editing alike. No per-tool list to fall out of date.
static const char* ANVIL_ALLOWED_TOOLS = "mcp__anvil-cad";

// Seed for the user-editable system prompt (<settings>/anvil_ai_prompt.txt). Kept short:
// the SKiDL server's own tool instructions carry the deep workflow rules.
static const char* ANVIL_DEFAULT_PROMPT =
        "You are Anvil AI, the assistant inside the Anvil CAD schematic/PCB suite.\n"
        "You have ONE MCP server, `anvil-cad`, providing every tool you need:\n"
        "  - Circuit generation: parts, build, create_pcb, run_drc, verify_board, "
        "generate_bom, export_manufacturing, ...\n"
        "  - Live editing of the open editors: read_live, edit_schematic_live(ops=[...]),\n"
        "    edit_board_live(ops=[...]), check_live.\n"
        "\n"
        "Workflow for a NEW circuit request: search parts -> build -> create_pcb (if asked) "
        "-> open in the app -> verify with check_live or run_drc. Always report real file "
        "paths and real ERC/DRC results; never claim success without them.\n"
        "\n"
        "For CHANGES to the open design: batch every edit from one user request into a "
        "single edit_schematic_live / edit_board_live call using the ops array, then "
        "check_live to confirm.\n"
        "\n"
        "If a live tool reports that the tool server is not running, tell the user to "
        "click AnvilCAD MCP -> Start in the app menu.\n"
        "\n"
        "Be concise. Answer in the user's language when it is not English.\n";


ANVIL_AI_AGENT::ANVIL_AI_AGENT( KIWAY* aKiway, wxWindow* aParent, WEBVIEW_PANEL* aPanel ) :
        m_kiway( aKiway ),
        m_parent( aParent ),
        m_panel( aPanel ),
        m_busy( false ),
        m_cancel( false ),
        m_sawReply( false ),
        m_turnClosed( true ),
        m_heartbeatRun( false ),
        m_child( nullptr )
{
}


ANVIL_AI_AGENT::~ANVIL_AI_AGENT()
{
    // The panel is going away, so stop talking to it. The turn worker is detached, so give
    // it a bounded chance to unwind (killing the CLI unblocks its read loop) before the
    // members it touches go out from under it.
    m_cancel = true;
    killChild();

    for( int i = 0; i < 60 && m_busy.load(); ++i )
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

    stopHeartbeat();
}


void ANVIL_AI_AGENT::Attach()
{
    if( !m_panel )
        return;

    m_panel->AddMessageHandler( wxS( "anvilSend" ),
            [this]( const wxString& aMsg ) { onBridgeMessage( aMsg ); } );
}


void ANVIL_AI_AGENT::SetDocumentContext( const wxString& aProjectPath,
                                         const wxString& aSchematicFile,
                                         const wxString& aBoardFile )
{
    // A different project must not inherit the previous design's conversation.
    if( !m_projectPath.IsEmpty() && !aProjectPath.IsEmpty() && aProjectPath != m_projectPath )
        m_session.Clear();

    if( !aProjectPath.IsEmpty() )
        m_projectPath = aProjectPath;

    if( !aSchematicFile.IsEmpty() )
        m_schematicFile = aSchematicFile;

    if( !aBoardFile.IsEmpty() )
        m_boardFile = aBoardFile;

    pushContextToPage();
}


void ANVIL_AI_AGENT::NotifyPanelOpened()
{
    if( m_panel )
        m_panel->RunScriptAsync( wxS( "if(window.anvilPanelOpened)window.anvilPanelOpened();" ) );
}


void ANVIL_AI_AGENT::pushContextToPage()
{
    if( !m_panel )
        return;

    // Argument-per-argument as JSON string literals so backslashes survive.
    json args = json::array( { std::string( m_projectPath.utf8_str() ),
                               std::string( m_schematicFile.utf8_str() ),
                               std::string( m_boardFile.utf8_str() ),
                               std::string() } );

    wxString script;
    script << wxS( "if(window.anvilSetContext)window.anvilSetContext(" )
           << wxString::FromUTF8( args[0].dump() ) << wxS( "," )
           << wxString::FromUTF8( args[1].dump() ) << wxS( "," )
           << wxString::FromUTF8( args[2].dump() ) << wxS( "," )
           << wxString::FromUTF8( args[3].dump() ) << wxS( ");" );

    m_panel->RunScriptAsync( script );
}


void ANVIL_AI_AGENT::emit( const json& aMsg )
{
    // chat.html exposes window.anvilRecv(jsonString). json(string).dump() yields a safe,
    // escaped JS string literal.
    std::string payload = aMsg.dump();
    wxString    script = wxS( "if(window.anvilRecv)window.anvilRecv(" )
                         + wxString::FromUTF8( json( payload ).dump() ) + wxS( ");" );

    WEBVIEW_PANEL* panel = m_panel;

    if( !panel )
        return;

    panel->CallAfter( [panel, script]() { panel->RunScriptAsync( script ); } );
}


// ---------------------------------------------------------------------------------------
// Turn lifecycle: phase, heartbeat, end, cancel
// ---------------------------------------------------------------------------------------

void ANVIL_AI_AGENT::setPhase( const wxString& aPhase )
{
    {
        std::lock_guard<std::mutex> lock( m_phaseMutex );

        if( m_phase == aPhase )
            return;                     // same step as before — don't spam the timeline

        m_phase = aPhase;
    }

    emit( { { "kind", "status" }, { "text", std::string( aPhase.utf8_str() ) } } );
}


long ANVIL_AI_AGENT::elapsedSeconds() const
{
    return (long) std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now() - m_turnStart ).count();
}


void ANVIL_AI_AGENT::startHeartbeat( const wxString& aPhase )
{
    stopHeartbeat();

    {
        std::lock_guard<std::mutex> lock( m_phaseMutex );
        m_phase.Clear();                // force the first setPhase() through
    }

    setPhase( aPhase );

    std::lock_guard<std::mutex> lock( m_hbMutex );

    m_turnStart = std::chrono::steady_clock::now();
    m_heartbeatRun = true;

    // Claude can be silent for minutes on a long prompt; silence cannot be allowed to mean
    // anything. The panel keeps its "working" state on the strength of these ticks alone.
    m_heartbeat = std::thread( [this]()
    {
        int ticks = 0;

        while( m_heartbeatRun.load() )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );

            if( !m_heartbeatRun.load() )
                break;

            if( ++ticks % 15 != 0 )     // ~every 3 s
                continue;

            wxString phase;
            {
                std::lock_guard<std::mutex> plock( m_phaseMutex );
                phase = m_phase;
            }

            emit( { { "kind", "status" },
                    { "text", std::string( wxString::Format( wxS( "%s · %lds" ), phase,
                                                             elapsedSeconds() ).utf8_str() ) } } );
        }
    } );
}


void ANVIL_AI_AGENT::stopHeartbeat()
{
    std::lock_guard<std::mutex> lock( m_hbMutex );

    m_heartbeatRun = false;

    if( m_heartbeat.joinable() )
        m_heartbeat.join();
}


void ANVIL_AI_AGENT::endTurn( const std::string& aReason, const wxString& aErrText )
{
    // Every exit path calls this, including two that can race (the worker finishing and
    // the destructor). Latch it so the panel gets exactly one closing event.
    bool expected = false;

    if( !m_turnClosed.compare_exchange_strong( expected, true ) )
        return;

    stopHeartbeat();
    m_busy = false;

    if( aReason == "error" && !aErrText.IsEmpty() )
        emit( { { "kind", "error" }, { "text", std::string( aErrText.utf8_str() ) } } );
    else
        emit( { { "kind", "done" }, { "reason", aReason } } );
}


void ANVIL_AI_AGENT::cancelTurn()
{
    if( !m_busy )
        return;

    m_cancel = true;
    setPhase( _( "Stopping" ) );

    // Killing the child unblocks the worker's ReadFile; the worker then closes the turn.
    // Deliberately not calling endTurn() here: clearing m_busy while the worker still runs
    // would let a second turn start on top of the first.
    killChild();
}


void ANVIL_AI_AGENT::killChild()
{
#ifdef _WIN32
    std::lock_guard<std::mutex> lock( m_childMutex );

    if( m_child )
    {
        TerminateProcess( (HANDLE) m_child, 1 );
        m_child = nullptr;              // the worker still owns (and closes) the handle
    }
#endif
}


// ---------------------------------------------------------------------------------------
// Bridge dispatch
// ---------------------------------------------------------------------------------------

void ANVIL_AI_AGENT::onBridgeMessage( const wxString& aJson )
{
    json msg;

    try
    {
        msg = json::parse( std::string( aJson.utf8_str() ) );
    }
    catch( const std::exception& )
    {
        return;
    }

    std::string kind = msg.value( "kind", std::string() );

    if( kind == "hello" )
    {
        emit( { { "kind", "ready" } } );
    }
    else if( kind == "ping" )
    {
        emit( { { "kind", "pong" } } );
    }
    else if( kind == "cancel" )
    {
        cancelTurn();
    }
    else if( kind == "reset" )
    {
        if( m_busy )
        {
            emit( { { "kind", "error" },
                    { "text", "Still working on the previous request — press Stop first." } } );
            return;
        }

        m_session.Clear();              // fresh CLI session on the next turn
        emit( { { "kind", "status" }, { "text", "New conversation." } } );
    }
    else if( kind == "message" )
    {
        // Never drop a message silently: the page has already drawn the user's bubble, so
        // with no reply and no explanation "busy" looks identical to "crashed".
        if( m_busy )
        {
            emit( { { "kind", "error" },
                    { "text", "Still working on your previous request — press Stop to cancel "
                              "it, or wait for it to finish." } } );
            return;
        }

        wxString text = wxString::FromUTF8( msg.value( "text", std::string() ) );

        if( text.IsEmpty() )
            return;

        m_cancel = false;
        m_sawReply = false;
        m_turnClosed = false;
        m_busy = true;

        // Start talking before the worker even spawns: on a long prompt the first real
        // event from the model can be a while away.
        startHeartbeat( _( "Preparing your request" ) );

        std::thread( [this, text]() { runTurn( text ); } ).detach();
    }
}


// ---------------------------------------------------------------------------------------
// Discovery — nothing hardcoded to a machine, everything overridable
// ---------------------------------------------------------------------------------------

wxString ANVIL_AI_AGENT::resolveClaudeExe() const
{
    wxString v;

    if( wxGetEnv( wxS( "ANVIL_CLAUDE_EXE" ), &v ) && !v.IsEmpty() && wxFileExists( v ) )
        return v;

    // npm global layout: %APPDATA%/npm/node_modules/@anthropic-ai/claude-code/bin/claude.exe
    wxString appdata;

    if( wxGetEnv( wxS( "APPDATA" ), &appdata ) && !appdata.IsEmpty() )
    {
        wxFileName fn( appdata, wxS( "claude.exe" ) );
        fn.AppendDir( wxS( "npm" ) );
        fn.AppendDir( wxS( "node_modules" ) );
        fn.AppendDir( wxS( "@anthropic-ai" ) );
        fn.AppendDir( wxS( "claude-code" ) );
        fn.AppendDir( wxS( "bin" ) );

        if( fn.FileExists() )
            return fn.GetFullPath();
    }

    return wxS( "claude" );             // last resort: rely on PATH
}


wxString ANVIL_AI_AGENT::resolvePython() const
{
    wxString v;

    if( wxGetEnv( wxS( "ANVIL_AI_PYTHON" ), &v ) && !v.IsEmpty() && wxFileExists( v ) )
        return v;

    // Per-user CPython installs: %LOCALAPPDATA%/Programs/Python/Python3xx/python.exe.
    // Scan rather than pin a version.
    wxString localAppData;

    if( wxGetEnv( wxS( "LOCALAPPDATA" ), &localAppData ) && !localAppData.IsEmpty() )
    {
        wxString pyRoot = localAppData + wxFileName::GetPathSeparator() + wxS( "Programs" )
                          + wxFileName::GetPathSeparator() + wxS( "Python" );

        if( wxDir::Exists( pyRoot ) )
        {
            wxDir    dir( pyRoot );
            wxString sub;
            wxString best;

            bool cont = dir.GetFirst( &sub, wxS( "Python3*" ), wxDIR_DIRS );

            while( cont )
            {
                if( sub > best )        // lexically newest version wins
                    best = sub;

                cont = dir.GetNext( &sub );
            }

            if( !best.IsEmpty() )
            {
                wxFileName fn( pyRoot + wxFileName::GetPathSeparator() + best,
                               wxS( "python.exe" ) );

                if( fn.FileExists() )
                    return fn.GetFullPath();
            }
        }
    }

    return wxS( "python" );             // PATH fallback (the py launcher shim also works)
}


wxString ANVIL_AI_AGENT::resolveServerScript() const
{
    wxString v;

    if( wxGetEnv( wxS( "ANVIL_MCP_SCRIPT" ), &v ) && !v.IsEmpty() && wxFileExists( v ) )
        return v;

    // Installed layout: <stock data>/ai/skidl_mcp_server.py (share/anvil/ai/...).
    wxFileName installed( PATHS::GetStockDataPath( false ), wxS( "skidl_mcp_server.py" ) );
    installed.AppendDir( wxS( "ai" ) );

    if( installed.FileExists() )
        return installed.GetFullPath();

    // Dev layout: some ancestor of the exe holds skidl/skidl_mcp_server.py
    // (e.g. <root>/kicad-source-mirror/build/install/.../bin -> <root>/skidl/...).
    wxFileName walk( wxStandardPaths::Get().GetExecutablePath() );
    walk.SetFullName( wxEmptyString );

    for( int up = 0; up < 8 && walk.GetDirCount() > 0; ++up )
    {
        wxFileName probe( walk );
        probe.AppendDir( wxS( "skidl" ) );
        probe.SetFullName( wxS( "skidl_mcp_server.py" ) );

        if( probe.FileExists() )
            return probe.GetFullPath();

        walk.RemoveLastDir();
    }

    return wxEmptyString;
}


wxString ANVIL_AI_AGENT::writeMcpConfig( wxString* aWhatIsMissing )
{
    wxString python = resolvePython();
    wxString script = resolveServerScript();

    if( script.IsEmpty() )
    {
        if( aWhatIsMissing )
        {
            *aWhatIsMissing = _( "The Anvil AI tool server script (skidl_mcp_server.py) was "
                                 "not found. Set ANVIL_MCP_SCRIPT to its full path." );
        }

        return wxEmptyString;
    }

    json cfg;
    cfg["mcpServers"]["anvil-cad"]["command"] = std::string( python.utf8_str() );
    cfg["mcpServers"]["anvil-cad"]["args"] =
            json::array( { std::string( script.utf8_str() ) } );

    wxFileName fn( SETTINGS_MANAGER::GetUserSettingsPath(), wxS( "anvil_mcp.json" ) );
    wxFFile    f( fn.GetFullPath(), wxS( "wb" ) );

    if( !f.IsOpened() )
    {
        if( aWhatIsMissing )
            *aWhatIsMissing = _( "Could not write anvil_mcp.json in the settings folder." );

        return wxEmptyString;
    }

    std::string dump = cfg.dump( 2 );
    f.Write( dump.data(), dump.size() );
    f.Close();

    return fn.GetFullPath();
}


wxString ANVIL_AI_AGENT::writeSystemPromptFile()
{
    const wxString cfgDir = SETTINGS_MANAGER::GetUserSettingsPath();

    // Seed the editable base prompt on first use; afterwards the user's edits win.
    wxFileName baseFn( cfgDir, wxS( "anvil_ai_prompt.txt" ) );

    if( !baseFn.FileExists() )
    {
        wxFFile seed( baseFn.GetFullPath(), wxS( "wb" ) );

        if( seed.IsOpened() )
        {
            seed.Write( ANVIL_DEFAULT_PROMPT, strlen( ANVIL_DEFAULT_PROMPT ) );
            seed.Close();
        }
    }

    wxString base;
    {
        wxFFile f( baseFn.GetFullPath(), wxS( "rb" ) );

        if( f.IsOpened() )
            f.ReadAll( &base, wxConvUTF8 );
    }

    // Live context rides along so the model behaves like an assistant that can see the
    // project rather than one that has to ask.
    wxString full = base;

    if( !m_projectPath.IsEmpty() )
        full << wxS( "\n\nCurrent project folder: " ) << m_projectPath;

    if( !m_schematicFile.IsEmpty() )
        full << wxS( "\nOpen schematic: " ) << m_schematicFile;

    if( !m_boardFile.IsEmpty() )
        full << wxS( "\nOpen board: " ) << m_boardFile;

    wxFileName outFn( cfgDir, wxS( "anvil_ai_sysprompt.tmp" ) );
    wxFFile    out( outFn.GetFullPath(), wxS( "wb" ) );

    if( out.IsOpened() )
    {
        out.Write( full, wxConvUTF8 );
        out.Close();
    }

    return outFn.GetFullPath();
}


// ---------------------------------------------------------------------------------------
// The turn: spawn claude.exe -p, stream its events into the page
// ---------------------------------------------------------------------------------------

void ANVIL_AI_AGENT::runTurn( wxString aUserText )
{
#ifdef _WIN32
    setPhase( _( "Starting the AI engine" ) );

    wxString claudeExe = resolveClaudeExe();

    if( claudeExe != wxS( "claude" ) && !wxFileExists( claudeExe ) )
    {
        endTurn( "error", _( "Claude Code CLI not found. Install it with: "
                             "npm install -g @anthropic-ai/claude-code" ) );
        return;
    }

    wxString missing;
    wxString mcpCfg = writeMcpConfig( &missing );

    if( mcpCfg.IsEmpty() )
    {
        endTurn( "error", missing );
        return;
    }

    wxString sysPrompt = writeSystemPromptFile();

    // The user's text goes through a file (stdin) so nothing needs shell escaping.
    wxFileName promptFn( SETTINGS_MANAGER::GetUserSettingsPath(),
                         wxS( "anvil_ai_prompt_in.tmp" ) );
    {
        wxFFile f( promptFn.GetFullPath(), wxS( "wb" ) );

        if( f.IsOpened() )
        {
            f.Write( aUserText, wxConvUTF8 );
            f.Close();
        }
    }

    // Fixed flags + our own paths/session id only — no user text on the command line.
    wxString cmd;
    cmd << wxS( "\"" ) << claudeExe << wxS( "\"" )
        << wxS( " -p --output-format stream-json --verbose" )
        << wxS( " --mcp-config \"" ) << mcpCfg << wxS( "\"" )
        << wxS( " --append-system-prompt-file \"" ) << sysPrompt << wxS( "\"" )
        << wxS( " --allowedTools " ) << wxString::FromUTF8( ANVIL_ALLOWED_TOOLS );

    if( !m_session.IsEmpty() )
        cmd << wxS( " --resume " ) << m_session;

    // --- spawn: stdin = prompt file, stdout+stderr = one pipe, no console window ---
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof( sa );
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hOutRd = nullptr, hOutWr = nullptr;

    if( !CreatePipe( &hOutRd, &hOutWr, &sa, 0 ) )
    {
        endTurn( "error", _( "Could not create the output pipe for the AI engine." ) );
        return;
    }

    SetHandleInformation( hOutRd, HANDLE_FLAG_INHERIT, 0 );

    HANDLE hIn = CreateFileW( promptFn.GetFullPath().wc_str(), GENERIC_READ, FILE_SHARE_READ,
                              &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

    STARTUPINFOW si;
    ZeroMemory( &si, sizeof( si ) );
    si.cb = sizeof( si );
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hIn;
    si.hStdOutput = hOutWr;
    si.hStdError = hOutWr;

    PROCESS_INFORMATION pi;
    ZeroMemory( &pi, sizeof( pi ) );

    std::wstring cmdLine( cmd.wc_str() );
    cmdLine.push_back( L'\0' );         // CreateProcessW may modify the buffer

    BOOL okSpawn = CreateProcessW( nullptr, &cmdLine[0], nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi );

    CloseHandle( hOutWr );              // parent doesn't write

    if( hIn != INVALID_HANDLE_VALUE )
        CloseHandle( hIn );

    if( !okSpawn )
    {
        CloseHandle( hOutRd );
        endTurn( "error", _( "Failed to launch the AI engine process." ) );
        return;
    }

    // Publish the handle so Stop can kill the child and unblock the ReadFile below.
    {
        std::lock_guard<std::mutex> lock( m_childMutex );
        m_child = pi.hProcess;
    }

    if( m_cancel )                      // Stop pressed between the flag and the handle
        killChild();

    setPhase( _( "Thinking" ) );

    // Read stdout, dispatch newline-delimited stream-json events. Non-JSON lines are the
    // CLI's own stderr (it shares the pipe) — keep them so a run that dies without
    // answering can say why.
    std::string accum;
    std::string diagnostics;
    char        buf[8192];
    DWORD       nRead = 0;

    auto feed = [&]( const std::string& aLine )
    {
        if( !handleStreamEvent( aLine ) && diagnostics.size() < 8000 )
            diagnostics += aLine + "\n";
    };

    while( ReadFile( hOutRd, buf, sizeof( buf ), &nRead, nullptr ) && nRead > 0 )
    {
        accum.append( buf, nRead );

        size_t nl;

        while( ( nl = accum.find( '\n' ) ) != std::string::npos )
        {
            std::string line = accum.substr( 0, nl );
            accum.erase( 0, nl + 1 );
            feed( line );
        }
    }

    if( !accum.empty() )
        feed( accum );

    WaitForSingleObject( pi.hProcess, INFINITE );

    DWORD exitCode = 0;
    GetExitCodeProcess( pi.hProcess, &exitCode );

    {
        std::lock_guard<std::mutex> lock( m_childMutex );
        m_child = nullptr;              // before CloseHandle, so Stop can't hit a dead handle
    }

    CloseHandle( hOutRd );
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );

    if( m_cancel )
    {
        endTurn( "cancelled" );
        return;
    }

    // A turn that ends having said nothing is the failure the user actually sees. Report
    // the exit code and whatever the CLI printed on the way out.
    if( !m_sawReply || exitCode != 0 )
    {
        wxString detail = wxString::FromUTF8( diagnostics.c_str() ).Trim().Trim( false );

        if( detail.Length() > 2000 )
            detail = detail.Left( 2000 ) + wxS( "\n..." );

        wxString err = m_sawReply
                ? wxString::Format( _( "The AI engine exited with code %lu." ),
                                    (unsigned long) exitCode )
                : wxString::Format( _( "The AI engine stopped after %lds without answering "
                                       "(exit code %lu)." ),
                                    elapsedSeconds(), (unsigned long) exitCode );

        if( !detail.IsEmpty() )
            err << wxS( "\n\n" ) << detail;
        else if( !m_sawReply )
            err << wxS( " " )
                << _( "Run `claude` once in a terminal to check that you are signed in." );

        endTurn( "error", err );
        return;
    }
#else
    endTurn( "error", _( "The Anvil AI chat is currently Windows-only." ) );
    return;
#endif

    endTurn( "ok" );                    // idempotent when a path above already closed it
}


bool ANVIL_AI_AGENT::handleStreamEvent( const std::string& aLine )
{
    // The pipe hands us raw bytes, so a line can still carry the CR of a CRLF.
    std::string line = aLine;

    while( !line.empty() && ( line.back() == '\r' || line.back() == '\n' ) )
        line.pop_back();

    if( line.find_first_not_of( " \t" ) == std::string::npos )
        return true;                    // blank — handled, nothing worth reporting

    json j;

    try
    {
        j = json::parse( line );
    }
    catch( const std::exception& )
    {
        return false;                   // not stream-json: CLI stderr, caller keeps it
    }

    std::string type = j.value( "type", std::string() );

    if( type == "system" )
    {
        if( j.value( "subtype", std::string() ) == "init" && j.contains( "session_id" ) )
        {
            m_session = wxString::FromUTF8( j["session_id"].get<std::string>() );
            setPhase( _( "Reading your design" ) );
        }
    }
    else if( type == "assistant" )
    {
        if( j.contains( "message" ) && j["message"].contains( "content" ) )
        {
            for( const json& block : j["message"]["content"] )
            {
                std::string bt = block.value( "type", std::string() );

                if( bt == "text" )
                {
                    std::string t = block.value( "text", std::string() );

                    if( !t.empty() )
                    {
                        m_sawReply = true;
                        setPhase( _( "Writing the answer" ) );
                        emit( { { "kind", "reply" }, { "text", t } } );
                    }
                }
                else if( bt == "thinking" )
                {
                    std::string t = block.value( "thinking", std::string() );

                    if( !t.empty() )
                        emit( { { "kind", "thinking" }, { "text", t } } );
                }
                else if( bt == "tool_use" )
                {
                    // Strip the mcp__anvil-cad__ prefix for a readable status line.
                    std::string name = block.value( "name", std::string() );
                    size_t      p = name.rfind( "__" );

                    if( p != std::string::npos )
                        name = name.substr( p + 2 );

                    setPhase( wxString::Format( _( "Running %s" ),
                                                wxString::FromUTF8( name.c_str() ) ) );
                }
            }
        }
    }
    else if( type == "user" )
    {
        // A tool_result coming back: that step is done, the model is thinking again.
        setPhase( _( "Thinking" ) );
    }
    else if( type == "result" )
    {
        if( j.contains( "session_id" ) )
            m_session = wxString::FromUTF8( j["session_id"].get<std::string>() );

        if( j.value( "is_error", false ) )
        {
            // Surface it; closing the turn stays with runTurn() so a second turn can't
            // start while this one is still draining the pipe.
            std::string msg = j.value( "result", std::string( "The AI engine reported an "
                                                              "error." ) );
            emit( { { "kind", "error" }, { "text", msg } } );
        }
    }

    return true;
}
