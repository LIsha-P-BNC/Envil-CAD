/*
 * Anvil AI — native chat agent. See anvil_ai_agent.h.
 */

#include <anvil_ai/anvil_ai_agent.h>

#include <cstring>
#include <thread>

#include <wx/base64.h>
#include <wx/buffer.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/translation.h>
#include <wx/ffile.h>
#include <wx/filefn.h>          // wxFileExists
#include <wx/log.h>             // wxLogNull -- keep settings IO from popping a modal
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>           // wxGetEnv

#include <anvil_auth/anvil_auth.h>
#include <anvil_auth/anvil_auth_config.h>
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
#include <winsock2.h>            // bridge port probe -- must precede windows.h
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment( lib, "ws2_32.lib" )
#endif

#include <mutex>
#include <vector>

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
        "CONTINUE vs NEW project -- decide from context, do not ask by default. The window "
        "context below always shows the currently-open project and a summary of the design "
        "already in it (its sheets and what each contains). Before you build, look at that "
        "summary and at any file the user attached earlier in this conversation:\n"
        " - If a project is already open and the new request extends it (the next sheet of "
        "the same PDF/image, another block of the same circuit, or 'add this ...'), CONTINUE "
        "that same project. Do NOT create a fresh project and do NOT regenerate the existing "
        "sheets. Read what is already there, then ADD only the new sheet/section on top of "
        "it, keeping every existing sheet, part and net intact.\n"
        " - Start a NEW project only when the request is unrelated to the open design, or the "
        "user explicitly asks for a new/separate project. When genuinely unsure between "
        "extending and starting fresh, ask a single short question naming both choices.\n"
        " - A source document (PDF/image/text/URL/doc) attached earlier stays available for "
        "the whole conversation: reuse it for later sheets or additions instead of asking the "
        "user to send it again. The user may design just one part of a document first, then "
        "later ask for another part, an edit, or an added circuit -- treat the attachment as "
        "still in hand every time.\n"
        "\n"
        "For CHANGES to the open design: batch every edit from one user request into a "
        "single edit_schematic_live / edit_board_live call using the ops array, then "
        "check_live to confirm.\n"
        "\n"
        "When REPRODUCING a schematic from an attached document (PDF/image): read the "
        "connectivity strictly, net by net, before drawing anything. Two wires that merely "
        "CROSS are NOT connected — only a filled junction dot (or a shared endpoint / pin) "
        "connects them; a hop/jump-over arc explicitly means NO connection. Never draw one "
        "wire straight through another net to save routing: route around it, or connect the "
        "two points by placing the same net label at both ends. Add a junction only where "
        "the source drawing shows a dot. After drawing, re-check each net against the "
        "source before reporting done.\n"
        "\n"
        "The in-app chat starts its bundled local SKiDL tools automatically. Never ask "
        "the user to click AnvilCAD MCP -> Start: that menu is only for an external MCP "
        "client and is not required for this chat.\n"
        "\n"
        "Be concise. Always answer in English, regardless of what language the user writes in.\n";


// ---------------------------------------------------------------------------------------
// AI account (auth mode): "shared" = built-in API key, "own" = the user's Claude
// account (browser sign-in), "" = auto (own login wins when present). The choice is
// the USER'S -- persisted in <settings>/anvil_ai_auth.txt and surfaced in the chat
// header, never silently decided for a machine that has both.
// ---------------------------------------------------------------------------------------

static wxString authModeFile()
{
    return wxFileName( SETTINGS_MANAGER::GetUserSettingsPath(),
                       wxS( "anvil_ai_auth.txt" ) ).GetFullPath();
}


static wxString readAuthMode()
{
    const wxString path = authModeFile();
    wxString       v;

    // A missing file is the NORMAL state (first run / no explicit account choice yet) --
    // it means "auto".  Bail before touching wxFFile: opening a non-existent file makes
    // wxFFile raise a wxLogSysError that pops up as a modal "Anvil Error" dialog on every
    // editor open.  wxLogNull additionally swallows any transient read error for the same
    // reason -- this helper must never surface UI.
    if( !wxFileName::FileExists( path ) )
        return wxEmptyString;                       // auto

    wxLogNull noLog;
    wxFFile   f( path, wxS( "rb" ) );

    if( f.IsOpened() )
        f.ReadAll( &v, wxConvUTF8 );

    v.Trim().Trim( false );

    if( v == wxS( "own" ) || v == wxS( "shared" ) )
        return v;

    return wxEmptyString;                       // auto
}


static void writeAuthMode( const wxString& aMode )
{
    wxLogNull noLog;                                // never surface a modal from a settings write
    wxFFile   f( authModeFile(), wxS( "wb" ) );

    if( f.IsOpened() )
        f.Write( aMode, wxConvUTF8 );
}


static bool hasOwnClaudeLogin()
{
    wxString home;

    if( wxGetEnv( wxS( "USERPROFILE" ), &home ) && !home.IsEmpty() )
    {
        wxFileName cred( home, wxS( ".credentials.json" ) );
        cred.AppendDir( wxS( ".claude" ) );
        return cred.FileExists();
    }

    return false;
}


// ---------------------------------------------------------------------------------------
// Shared-credential helpers. The deployment's AI credentials live in the .env beside the
// exe: either a direct Anthropic key (ClaudeApiKey), or a proxy base URL (ClaudeBaseUrl).
// A LOOPBACK base URL means the bundled OpenAI bridge (bin/ai/bridge): it forwards to the
// OpenAI API with the same .env's OpenAiApiKey, and the app is responsible for starting
// it before the engine's first request.
// ---------------------------------------------------------------------------------------

// How long a cold bridge gets to bind its port before the engine is launched anyway
// (the engine's own request retries cover a slightly late start).
static const int BRIDGE_START_TIMEOUT_MS = 20000;


// Split "http://host:port/..." into host and port (port 0 when the URL names none).
static bool parseHostPort( const wxString& aUrl, wxString* aHost, unsigned short* aPort )
{
    wxString rest   = aUrl;
    int      scheme = rest.Find( wxS( "://" ) );

    if( scheme != wxNOT_FOUND )
        rest = rest.Mid( scheme + 3 );

    rest = rest.BeforeFirst( '/' );

    wxString host = rest.BeforeFirst( ':' );
    wxString port = rest.AfterFirst( ':' );

    if( host.IsEmpty() )
        return false;

    *aHost = host;

    unsigned long p = 0;

    if( !port.IsEmpty() && port.ToULong( &p ) && p > 0 && p <= 65535 )
        *aPort = static_cast<unsigned short>( p );
    else
        *aPort = 0;

    return true;
}


static bool isLoopbackHost( const wxString& aHost )
{
    return aHost == wxS( "127.0.0.1" ) || aHost.CmpNoCase( wxS( "localhost" ) ) == 0;
}


// True when the .env ships everything "API key" mode needs to work with no per-user
// login: a direct Anthropic key, a local bridge URL with the OpenAI key it forwards to,
// or a remote proxy URL (authenticated per user with the signed-in JWT).
static bool hasSharedAiCredentials()
{
    if( !ANVIL_AUTH_CONFIG::ClaudeApiKey().IsEmpty() )
        return true;

    const wxString base = ANVIL_AUTH_CONFIG::ClaudeBaseUrl();

    if( base.IsEmpty() )
        return false;

    wxString       host;
    unsigned short port = 0;

    if( parseHostPort( base, &host, &port ) && isLoopbackHost( host ) )
        return !ANVIL_AUTH_CONFIG::OpenAiApiKey().IsEmpty();    // bundled bridge

    return true;                                                // remote proxy
}


#ifdef _WIN32
// True when something is listening on 127.0.0.1:aPort (non-blocking connect + select).
static bool localPortOpen( unsigned short aPort, int aTimeoutMs )
{
    WSADATA wsa;

    if( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 )
        return false;

    bool   open = false;
    SOCKET s = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

    if( s != INVALID_SOCKET )
    {
        u_long nonblock = 1;
        ioctlsocket( s, FIONBIO, &nonblock );

        sockaddr_in addr;
        ZeroMemory( &addr, sizeof( addr ) );
        addr.sin_family = AF_INET;
        addr.sin_port   = htons( aPort );
        inet_pton( AF_INET, "127.0.0.1", &addr.sin_addr );

        ::connect( s, reinterpret_cast<sockaddr*>( &addr ), sizeof( addr ) );

        fd_set wr, ex;
        FD_ZERO( &wr );
        FD_ZERO( &ex );
        FD_SET( s, &wr );
        FD_SET( s, &ex );

        timeval tv;
        tv.tv_sec  = aTimeoutMs / 1000;
        tv.tv_usec = ( aTimeoutMs % 1000 ) * 1000;

        // A refused connect lands in the EXCEPT set on Windows, success in the WRITE set.
        if( select( 0, nullptr, &wr, &ex, &tv ) > 0 && FD_ISSET( s, &wr ) )
            open = true;

        closesocket( s );
    }

    WSACleanup();
    return open;
}


// Start the bundled OpenAI bridge if the base URL points at this machine and nothing is
// listening yet. Runs on the (detached) turn thread, so the startup wait cannot freeze
// the UI. The bridge is spawned hidden and tied to a kill-on-close job object so every
// process it starts (cmd -> python) dies with the app.
static void ensureLocalBridgeRunning( const wxString& aBaseUrl )
{
    static std::mutex s_bridgeMutex;
    std::lock_guard<std::mutex> lock( s_bridgeMutex );

    wxString       host;
    unsigned short port = 0;

    if( !parseHostPort( aBaseUrl, &host, &port ) || !isLoopbackHost( host ) || port == 0 )
        return;                                     // remote proxy: nothing to start

    if( localPortOpen( port, 250 ) )
        return;                                     // already up (earlier turn / manual)

    wxString   exeDir = wxFileName( wxStandardPaths::Get().GetExecutablePath() ).GetPath();
    wxFileName bat( exeDir, wxS( "start_bridge.bat" ) );
    bat.AppendDir( wxS( "ai" ) );
    bat.AppendDir( wxS( "bridge" ) );

    if( !bat.FileExists() )
        return;                                     // not a bridge deployment

    // cmd.exe /c "<bat>" -- outer quotes protect the inner quoted path.
    std::wstring cmdLine =
            L"cmd.exe /c \"\"" + std::wstring( bat.GetFullPath().wc_str() ) + L"\"\"";
    std::vector<wchar_t> buf( cmdLine.begin(), cmdLine.end() );
    buf.push_back( L'\0' );

    STARTUPINFOW si;
    ZeroMemory( &si, sizeof( si ) );
    si.cb = sizeof( si );

    PROCESS_INFORMATION pi;
    ZeroMemory( &pi, sizeof( pi ) );

    // Suspended so the job object is attached before cmd can spawn python.
    if( !CreateProcessW( nullptr, buf.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi ) )
        return;

    // Kill-on-close job: the handle is intentionally kept for the process lifetime, so
    // the OS tears the bridge down whenever the app exits, however it exits.
    static HANDLE s_bridgeJob = nullptr;

    if( !s_bridgeJob )
    {
        s_bridgeJob = CreateJobObjectW( nullptr, nullptr );

        if( s_bridgeJob )
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim;
            ZeroMemory( &lim, sizeof( lim ) );
            lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject( s_bridgeJob, JobObjectExtendedLimitInformation, &lim,
                                     sizeof( lim ) );
        }
    }

    if( s_bridgeJob )
        AssignProcessToJobObject( s_bridgeJob, pi.hProcess );

    ResumeThread( pi.hThread );
    CloseHandle( pi.hThread );
    CloseHandle( pi.hProcess );

    // Give it a moment to bind the port; the engine's own retries cover the rest.
    for( int waited = 0; waited < BRIDGE_START_TIMEOUT_MS; waited += 500 )
    {
        if( localPortOpen( port, 250 ) )
            break;

        Sleep( 500 );
    }
}
#endif // _WIN32


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
    loadSessionState();
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
    // A different project must not inherit the previous design's CLI conversation, so the
    // resumable session is dropped on a real project change.
    //
    // Attachments are deliberately NOT dropped here. A multi-sheet flow (reproduce a PDF
    // one sheet at a time) creates a brand-new project on the first sheet and the app then
    // auto-opens it; that fires this same path-changed branch. Clearing the attachment list
    // there made the second-sheet request arrive with no source, so the model forgot the
    // PDF/image/text the user attached and asked for it again. The list holds only on-disk
    // file paths, re-listed each turn and re-read only when relevant, so carrying them across
    // projects is harmless and lets any earlier attachment (PDF / image / text / doc) stay
    // available for follow-up sheets. Continuity of the design itself comes from the live
    // project-analysis context injected each turn, not from the stale CLI conversation.
    if( !m_projectPath.IsEmpty() && !aProjectPath.IsEmpty() && aProjectPath != m_projectPath )
    {
        // ...but NOT when a turn is in flight: then the switch was made by the conversation
        // itself (build → auto-open of the project it just created), and wiping the session
        // here is exactly what made the AI forget its own work and restart the whole
        // workflow on the next turn. A user-driven switch happens between turns (m_busy
        // false) and still gets a fresh conversation.
        if( !m_busy.load() )
        {
            m_session.Clear();
            m_savedSession.Clear();
        }
    }

    if( !aProjectPath.IsEmpty() )
        m_projectPath = aProjectPath;

    if( !aSchematicFile.IsEmpty() )
        m_schematicFile = aSchematicFile;

    if( !aBoardFile.IsEmpty() )
        m_boardFile = aBoardFile;

    // A restart of the SAME project picks its conversation back up (the mid-turn spawn case
    // is already handled in loadSessionState via turn_active).
    if( m_session.IsEmpty() && !m_savedSession.IsEmpty() && !m_projectPath.IsEmpty()
            && m_projectPath == m_savedProject )
    {
        m_session = m_savedSession;
    }

    saveSessionState();
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


wxString ANVIL_AI_AGENT::buildProjectAnalysis() const
{
    if( m_projectPath.IsEmpty() )
        return wxEmptyString;

    wxFileName proj( m_projectPath );
    wxString   dir = proj.GetPath();

    if( dir.IsEmpty() || !wxDirExists( dir ) )
        return wxEmptyString;

    // Collect every schematic sheet on disk, whatever extension family is in use. A
    // hierarchical design yields several files; a single-sheet design (which may still
    // carry block sections drawn inside the one sheet) yields one. We never assume which
    // layout it is -- we report exactly what is present.
    wxArrayString sheets;
    wxDir::GetAllFiles( dir, &sheets, wxS( "*.kicad_sch" ), wxDIR_FILES );
    wxDir::GetAllFiles( dir, &sheets, wxS( "*.anvil_sch" ), wxDIR_FILES );

    if( sheets.IsEmpty() )
        return wxEmptyString;

    sheets.Sort();

    wxString summary;
    summary << wxS( "Design already in this project (" ) << (int) sheets.GetCount()
            << ( sheets.GetCount() == 1 ? wxS( " schematic sheet):" )
                                        : wxS( " schematic sheets):" ) );

    for( const wxString& path : sheets )
    {
        wxFileName sf( path );

        // Count placed component instances cheaply by scanning for the instance token.
        // In the sheet format only placed symbols carry "(lib_id " (library definitions
        // and child-sheet references do not), so this is an approximate part count that
        // works for any sheet regardless of hierarchy or internal block layout.
        wxString text;
        {
            wxLogNull noPopups;   // never pop a modal on an unreadable file
            wxFFile  f;

            if( f.Open( path, wxS( "rb" ) ) )
                f.ReadAll( &text );
        }

        int    parts = 0;
        size_t from  = 0;

        for( ;; )
        {
            size_t at = text.find( wxS( "(lib_id " ), from );

            if( at == wxString::npos )
                break;

            parts++;
            from = at + 8;
        }

        summary << wxS( "\n - " ) << sf.GetFullName() << wxS( " (~" ) << parts
                << ( parts == 1 ? wxS( " part)" ) : wxS( " parts)" ) );
    }

    return summary;
}


wxArrayString ANVIL_AI_AGENT::saveAttachments( const nlohmann::json& aAtts )
{
    // Each attachment arrives as a data: URL ("data:<mime>;base64,<payload>") plus a name.
    // Decode the base64 and write the bytes to <settings>/anvil_attachments/, returning the
    // real paths so the model can Read() them. Any single bad entry is skipped, never fatal.
    wxArrayString paths;

    wxFileName dir( SETTINGS_MANAGER::GetUserSettingsPath(), wxEmptyString );
    dir.AppendDir( wxS( "anvil_attachments" ) );

    if( !dir.DirExists() )
        dir.Mkdir( 0700, wxPATH_MKDIR_FULL );

    int idx = 0;

    for( const nlohmann::json& a : aAtts )
    {
        if( !a.is_object() )
            continue;

        std::string dataUrl = a.value( "dataUrl", std::string() );
        std::string name    = a.value( "name", std::string( "attachment" ) );

        size_t marker = dataUrl.find( ";base64," );

        if( marker == std::string::npos )
            continue;

        std::string b64 = dataUrl.substr( marker + 8 );
        wxMemoryBuffer buf = wxBase64Decode( b64.c_str(), b64.size(), wxBase64DecodeMode_SkipWS );

        if( buf.GetDataLen() == 0 )
            continue;

        // Sanitise the file name; keep the extension so the model knows the type.
        wxString       safe = wxString::FromUTF8( name );
        const wxString invalid = wxS( "\\/:*?\"<>|" );

        for( size_t k = 0; k < invalid.length(); ++k )
            safe.Replace( wxString( invalid[k] ), wxS( "_" ) );

        if( safe.IsEmpty() )
            safe = wxS( "attachment" );

        wxFileName out( dir.GetPath(), wxString::Format( wxS( "%d_%s" ), idx++, safe ) );

        wxFile f( out.GetFullPath(), wxFile::write );

        if( f.IsOpened() )
        {
            f.Write( buf.GetData(), buf.GetDataLen() );
            f.Close();
            paths.Add( out.GetFullPath() );
        }
    }

    return paths;
}


static wxString sessionStateFile()
{
    wxFileName fn( SETTINGS_MANAGER::GetUserSettingsPath(), wxS( "anvil_ai_session.json" ) );
    return fn.GetFullPath();
}


void ANVIL_AI_AGENT::saveSessionState()
{
    // Called from both the UI thread (SetDocumentContext, reset) and the turn worker
    // (session-id capture, closeTurn); the mutex keeps the file write atomic per caller.
    std::lock_guard<std::mutex> lock( m_stateMutex );

    nlohmann::json j;
    j["project"] = std::string( m_projectPath.utf8_str() );
    j["session"] = std::string( m_session.utf8_str() );

    // turn_active tells a NEW window spawned while this turn is still running (the shell
    // opens a different project as a separate window) that it was created BY this
    // conversation and should adopt it, not start blank.
    j["turn_active"] = m_busy.load();

    nlohmann::json atts = nlohmann::json::array();

    for( const wxString& p : m_sessionAttachments )
        atts.push_back( std::string( p.utf8_str() ) );

    j["attachments"] = atts;

    wxLogNull noLog;                                // a failed state write must never pop UI
    wxFFile   f( sessionStateFile(), wxS( "wb" ) );

    if( f.IsOpened() )
        f.Write( wxString::FromUTF8( j.dump( 2 ).c_str() ), wxConvUTF8 );
}


void ANVIL_AI_AGENT::loadSessionState()
{
    wxString path = sessionStateFile();

    if( !wxFileName::FileExists( path ) )
        return;

    wxLogNull noLog;
    wxFFile   f( path, wxS( "rb" ) );
    wxString  raw;

    if( !f.IsOpened() || !f.ReadAll( &raw, wxConvUTF8 ) )
        return;

    nlohmann::json j;

    try
    {
        j = nlohmann::json::parse( std::string( raw.utf8_str() ) );
    }
    catch( const std::exception& )
    {
        return;
    }

    // Attachments are plain disk paths: restore every one that still exists, so a follow-up
    // after a restart or in a freshly spawned window can still re-open the user's source
    // document instead of asking for it again.
    if( j.contains( "attachments" ) && j["attachments"].is_array() )
    {
        for( const nlohmann::json& a : j["attachments"] )
        {
            if( !a.is_string() )
                continue;

            wxString p = wxString::FromUTF8( a.get<std::string>() );

            if( wxFileName::FileExists( p ) && m_sessionAttachments.Index( p ) == wxNOT_FOUND )
                m_sessionAttachments.Add( p );
        }
    }

    m_savedSession = wxString::FromUTF8( j.value( "session", std::string() ) );
    m_savedProject = wxString::FromUTF8( j.value( "project", std::string() ) );

    // turn_active means the writing window was mid-turn when this instance started — i.e.
    // the conversation itself spawned this window (build → auto-open of a different project)
    // or the app died mid-turn. Either way, continuing that conversation is the right call,
    // so adopt the session immediately regardless of which project we end up showing.
    if( j.value( "turn_active", false ) && !m_savedSession.IsEmpty() )
        m_session = m_savedSession;
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
    saveSessionState();                 // records turn_active=false + the final session id

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
    else if( kind == "authmode" )
    {
        emit( { { "kind", "authmode" },
                { "mode", std::string( readAuthMode().utf8_str() ) },
                { "hasLogin", hasOwnClaudeLogin() },
                { "hasKey", hasSharedAiCredentials() } } );
    }
    else if( kind == "setauth" )
    {
        wxString mode = wxString::FromUTF8( msg.value( "mode", std::string() ) );

        if( mode == wxS( "own" ) || mode == wxS( "shared" ) )
            writeAuthMode( mode );

        emit( { { "kind", "authmode" },
                { "mode", std::string( readAuthMode().utf8_str() ) },
                { "hasLogin", hasOwnClaudeLogin() },
                { "hasKey", hasSharedAiCredentials() } } );
    }
    else if( kind == "claudelogin" )
    {
#ifdef _WIN32
        // Open the Claude CLI in its own visible console so the user can run /login
        // (the browser OAuth flow). The chat keeps working; once credentials exist,
        // "own" mode uses them on the next turn.
        wxString claudeExe = resolveClaudeExe();

        std::wstring cmdLine = L"\"" + std::wstring( claudeExe.wc_str() ) + L"\"";
        cmdLine.push_back( L'\0' );

        STARTUPINFOW si;
        ZeroMemory( &si, sizeof( si ) );
        si.cb = sizeof( si );

        PROCESS_INFORMATION pi;
        ZeroMemory( &pi, sizeof( pi ) );

        if( CreateProcessW( nullptr, &cmdLine[0], nullptr, nullptr, FALSE,
                            CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi ) )
        {
            CloseHandle( pi.hProcess );
            CloseHandle( pi.hThread );
        }
        else
        {
            emit( { { "kind", "error" },
                    { "text", "Could not open the Claude sign-in window." } } );
        }
#endif
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
        m_sessionAttachments.Clear();   // forget earlier attachments too
        m_savedSession.Clear();
        saveSessionState();             // an explicit New chat also forgets on disk
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

        // Attachments (image / PDF / text): save each new one to disk and REMEMBER its path
        // for the whole conversation. A follow-up turn (e.g. clicking a button) carries no
        // attachment of its own, so without this the model would forget an earlier PDF /
        // photo and start asking generic questions. Re-listing every session attachment on
        // each turn lets it re-open the file at any point.
        if( msg.contains( "attachments" ) && msg["attachments"].is_array()
                && !msg["attachments"].empty() )
        {
            for( const wxString& p : saveAttachments( msg["attachments"] ) )
            {
                if( m_sessionAttachments.Index( p ) == wxNOT_FOUND )
                    m_sessionAttachments.Add( p );
            }

            saveSessionState();
        }

        if( !m_sessionAttachments.IsEmpty() )
        {
            text << wxS( "\n\n(Files the user has attached in this conversation — open and "
                         "use them as needed; re-read them for detail:" );

            for( const wxString& p : m_sessionAttachments )
                text << wxS( "\n" ) << p;

            text << wxS( ")" );
        }

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

    // Bundled layout (like a VS Code extension's resources/native-binary): the standalone
    // claude.exe ships beside the app, so a shared install needs no npm / Node. Checked first
    // after the explicit override so an installed app is fully self-contained.
    wxFileName bundled( wxFileName( wxStandardPaths::Get().GetExecutablePath() ).GetPath(),
                        wxS( "claude.exe" ) );
    bundled.AppendDir( wxS( "ai" ) );
    bundled.AppendDir( wxS( "native-binary" ) );

    if( bundled.FileExists() )
        return bundled.GetFullPath();

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

    // Bundled standalone interpreter (skidl pre-installed) shipped beside the app, so a shared
    // install runs circuit builds with no user-installed Python. Checked before any system one.
    wxFileName bundledPy( wxFileName( wxStandardPaths::Get().GetExecutablePath() ).GetPath(),
                          wxS( "python.exe" ) );
    bundledPy.AppendDir( wxS( "ai" ) );
    bundledPy.AppendDir( wxS( "python" ) );

    if( bundledPy.FileExists() )
        return bundledPy.GetFullPath();

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
    // Marks the server that the caller is the built-in chat.  The same server may be
    // registered by a separate desktop agent, where the external MCP menu is relevant;
    // it must never leak that setup instruction into the app's own chat.
    cfg["mcpServers"]["anvil-cad"]["env"]["ANVIL_IN_APP_CHAT"] = "1";

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
    // project rather than one that has to ask. It is framed as REFERENCE, not a default
    // target: naming it "Current project folder" made the model treat every request as an
    // edit of the open project (it reported an unrelated open project as "already built"
    // when asked to make a NEW one). Make clear it only matters when editing THIS design.
    wxString full = base;

    if( !m_projectPath.IsEmpty() )
    {
        full << wxS( "\n\n--- WINDOW CONTEXT (the open project) ---\n"
                     "The project currently open in the app window is at: " )
             << m_projectPath << wxS( "." );

        if( !m_schematicFile.IsEmpty() )
            full << wxS( " Open schematic: " ) << m_schematicFile << wxS( "." );

        if( !m_boardFile.IsEmpty() )
            full << wxS( " Open board: " ) << m_boardFile << wxS( "." );

        wxString analysis = buildProjectAnalysis();

        if( !analysis.IsEmpty() )
        {
            full << wxS( "\n\n" ) << analysis
                 << wxS( "\n(The above is an approximate, always-current snapshot of what "
                         "already exists so you never have to ask what is in the project. It "
                         "may be a hierarchical design (several sheets) or a single sheet "
                         "holding block sections — do not assume; for exact parts, nets and "
                         "block layout call read_live before editing.)" );
        }

        full << wxS( "\n\nHow to use this: decide from the user's words whether the request "
                     "EXTENDS this open design or is a genuinely NEW one.\n"
                     " - EXTENDS (the next sheet of the same source, another block/section, "
                     "'add this ...', or a change to what is shown above): CONTINUE this same "
                     "project. Read what is already there first (read_live), keep every "
                     "existing sheet, part and net, and ADD only the new part on top — never "
                     "regenerate or overwrite the existing sheets.\n"
                     " - NEW / unrelated (names a different circuit, part, spec, or a new "
                     "project name, or explicitly asks for a new/separate project): build a "
                     "SEPARATE new project from the USER'S OWN WORDS ALONE; do NOT build "
                     "around, continue, or report the status of the open project above.\n"
                     " - A short reply (e.g. a clicked choice) that is ambiguous: re-read the "
                     "earlier conversation for the real subject; if still unclear, ask the "
                     "user which design they mean — never guess." );
    }

    // This is appended even when the user has an older, already-seeded prompt file.  It
    // prevents an old prompt (or a model's generic MCP recovery advice) from making the
    // built-in chat depend on the *external* MCP menu.  The CLI starts the bundled SKiDL
    // tool server from writeMcpConfig() for every turn; that transport is internal and
    // requires no action from the user.
    full << wxS( "\n\n--- BUILT-IN TOOL POLICY ---\n"
                 "This is the Anvil CAD in-app chat. Its local SKiDL circuit tools are "
                 "started automatically for this conversation. Do not tell the user to "
                 "start, connect, enable, or configure AnvilCAD MCP. The AnvilCAD MCP menu "
                 "is solely for a separate external AI client and is not a prerequisite for "
                 "building circuits, generating projects, or editing the open design here. "
                 "If a tool fails, report its actual error and continue with any available "
                 "local tool; never present external MCP setup as the required fix.\n" );

    // Appended unconditionally (even over an old user-edited prompt file): reading a
    // schematic's connectivity correctly is a correctness invariant, not a preference.
    // The concrete failure this prevents: a PDF drawn with wire crossings / jump-over
    // hops gets transcribed with the crossings fused into one net.
    full << wxS( "\n\n--- SCHEMATIC CONNECTION POLICY ---\n"
                 "When reproducing a schematic from an attached document (PDF/image), read "
                 "the connectivity STRICTLY, net by net, before drawing anything. Two wires "
                 "that merely CROSS are NOT connected; a hop/jump-over arc explicitly means "
                 "NO connection. Only a filled junction dot, a shared endpoint, or a pin "
                 "connects wires. Never draw a wire straight through another net to save "
                 "routing: route around it, or join distant points with the same net label "
                 "at both ends. add_wire never auto-connects at crossings or bends — call "
                 "add_junction ONLY where the source drawing shows a filled dot. After "
                 "drawing, verify every net against the source (and with check_live/ERC) "
                 "before reporting done.\n" );

    // Choice buttons collapse to their bare label when clicked: the app sends ONLY the
    // label text back as the next user message (see chat.html renderBubble). A label like
    // "Yes, build it" then arrives with no subject, and the model has been seen to refill
    // that vacuum from the WINDOW CONTEXT (building the open project instead of the one the
    // user actually asked for). Force every label to carry its own subject so a lone click
    // is still an unambiguous instruction.
    full << wxS( "\n\n--- CHOICE BUTTONS ---\n"
                 "You may offer quick choices by ending a reply with lines of the form "
                 "`::button:: <label>`. When the user clicks one, the app sends ONLY that "
                 "label text back as the next message — the surrounding reply is NOT "
                 "resent. So every label MUST be a complete, self-contained instruction "
                 "that still makes sense on its own: name the concrete subject inside the "
                 "label. Write `::button:: Build the 5V reverse-polarity supply now`, never "
                 "a bare `::button:: Yes, build it`. Likewise avoid bare `Yes`, `No`, "
                 "`Use it`, `Continue`, or `Do it` — always restate WHAT.\n" );

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
    //
    // --allowedTools mcp__anvil-cad auto-approves our MCP tools (no permission prompt).
    // --disallowedTools removes the CLI's own file/shell tools so the model CANNOT write
    //   files or run Python/commands itself: it is forced through the anvil-cad MCP tools,
    //   which do that work internally. This keeps raw "grant write / run python" permission
    //   prompts out of the chat entirely (they were leaking to the user before).
    wxString cmd;
    cmd << wxS( "\"" ) << claudeExe << wxS( "\"" )
        << wxS( " -p --output-format stream-json --verbose" )
        << wxS( " --mcp-config \"" ) << mcpCfg << wxS( "\"" )
        << wxS( " --append-system-prompt-file \"" ) << sysPrompt << wxS( "\"" )
        << wxS( " --allowedTools " ) << wxString::FromUTF8( ANVIL_ALLOWED_TOOLS )
        << wxS( " --disallowedTools \"Bash Write Edit MultiEdit NotebookEdit WebFetch WebSearch\"" );

    // The CLI can only read inside its working directory by default, which blocks the saved
    // chat attachments (<settings>/anvil_attachments) and the generated project files. Grant
    // read access so the model can open an attached datasheet PDF / circuit photo, and read
    // the project it builds.
    cmd << wxS( " --add-dir \"" ) << SETTINGS_MANAGER::GetUserSettingsPath() << wxS( "\"" );

    if( !m_projectPath.IsEmpty() )
    {
        cmd << wxS( " --add-dir \"" ) << m_projectPath << wxS( "\"" );

        // ...and the output root (parent of the project, e.g. F:\Anvil) where new projects
        // are built, so the model can read a design it just generated.
        wxFileName outRoot = wxFileName::DirName( m_projectPath );
        outRoot.RemoveLastDir();
        cmd << wxS( " --add-dir \"" ) << outRoot.GetPath() << wxS( "\"" );
    }

    if( !m_session.IsEmpty() )
        cmd << wxS( " --resume " ) << m_session;

    // Choose the AI engine's credentials. CreateProcessW below passes a nullptr environment, so
    // the child claude inherits whatever we set here. The USER picks the mode in the chat
    // header ("API key" = the deployment's shared credentials, billed centrally; "Claude
    // login" = their own browser-signed-in Claude account, billed to them). With no explicit
    // choice, the DEPLOYMENT's credentials win whenever they exist -- billing must be
    // predictable, never silently switched to whatever login a machine happens to have; a
    // user's own account is used only when they explicitly pick it (or nothing is shipped).
    // We only test the PRESENCE of claude's own credential file; its contents are never read.
    wxString authMode = readAuthMode();
    bool     userHasOwnClaudeLogin = hasOwnClaudeLogin();
    bool     useSharedKey;

    if( authMode == wxS( "shared" ) )
    {
        useSharedKey = true;
    }
    else if( authMode == wxS( "own" ) )
    {
        if( !userHasOwnClaudeLogin )
        {
            endTurn( "error",
                     _( "You chose your own Claude account but are not signed in yet. Click "
                        "the AI pill in the header, open the sign-in window, run /login, then "
                        "ask again." ) );
            return;
        }

        useSharedKey = false;
        // Their login must win: clear any inherited key/token/base-url that would
        // override it or reroute their account through the deployment's proxy.
        wxUnsetEnv( wxS( "ANTHROPIC_API_KEY" ) );
        wxUnsetEnv( wxS( "ANTHROPIC_AUTH_TOKEN" ) );
        wxUnsetEnv( wxS( "ANTHROPIC_BASE_URL" ) );
    }
    else
    {
        useSharedKey = hasSharedAiCredentials() || !userHasOwnClaudeLogin;      // auto
    }

    if( useSharedKey )
    {
        wxString claudeKey  = ANVIL_AUTH_CONFIG::ClaudeApiKey();
        wxString claudeBase = ANVIL_AUTH_CONFIG::ClaudeBaseUrl();

        if( !claudeBase.IsEmpty() )
        {
#ifdef _WIN32
            ensureLocalBridgeRunning( claudeBase );     // bundled OpenAI bridge, if any
#endif
            wxSetEnv( wxS( "ANTHROPIC_BASE_URL" ), claudeBase );
        }

        if( !claudeKey.IsEmpty() )
        {
            // Shared deployment key; make sure no stale bearer token overrides it.
            wxSetEnv( wxS( "ANTHROPIC_API_KEY" ), claudeKey );
            wxUnsetEnv( wxS( "ANTHROPIC_AUTH_TOKEN" ) );
        }
        else if( !claudeBase.IsEmpty() )
        {
            wxString       host;
            unsigned short port = 0;

            if( parseHostPort( claudeBase, &host, &port ) && isLoopbackHost( host ) )
            {
                // Bundled bridge: it authenticates upstream itself (OpenAiApiKey), but the
                // engine refuses to start with no credential at all -- give it a marker
                // key the bridge ignores.
                wxSetEnv( wxS( "ANTHROPIC_API_KEY" ), wxS( "anvil-local-bridge" ) );
                wxUnsetEnv( wxS( "ANTHROPIC_AUTH_TOKEN" ) );
            }
            else
            {
                // Per-user proxy: authenticate with the signed-in user's JWT bearer token.
                wxString userToken = ANVIL_AUTH::GetSessionToken();

                if( !userToken.IsEmpty() )
                {
                    wxSetEnv( wxS( "ANTHROPIC_AUTH_TOKEN" ), userToken );
                    wxUnsetEnv( wxS( "ANTHROPIC_API_KEY" ) );
                }
            }
        }
    }

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
            saveSessionState();     // persist ASAP: a window spawned mid-turn adopts this
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
        {
            m_session = wxString::FromUTF8( j["session_id"].get<std::string>() );
            saveSessionState();
        }

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
