/*
 * Envil AI — native agent driving the Anvil chat UI. See envil_ai_agent.h.
 */

#include <envil_ai/envil_ai_agent.h>

#include <thread>

#include <wx/msgdlg.h>
#include <wx/thread.h>          // wxSemaphore
#include <wx/translation.h>

#include <envil_ai/envil_ai_tool_bridge.h>
#include <widgets/webview_panel.h>

#include <wx/filename.h>
#include <wx/ffile.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>          // wxGetEnv
#include <wx/filefn.h>         // wxFileExists
#include <settings/settings_manager.h>

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

// The MCP tool names the CLI is allowed to run — our schematic tools only, so the CLI
// can't touch the filesystem or shell. Must match tools/envil-mcp's server name + tools.
static const char* ENVIL_MCP_ALLOWED_TOOLS =
        "mcp__envil-cad__get_schematic mcp__envil-cad__run_erc mcp__envil-cad__annotate "
        "mcp__envil-cad__add_component mcp__envil-cad__add_wire mcp__envil-cad__add_label "
        "mcp__envil-cad__add_junction mcp__envil-cad__add_no_connect mcp__envil-cad__edit_value "
        "mcp__envil-cad__move_component mcp__envil-cad__delete_component "
        "mcp__envil-cad__delete_at mcp__envil-cad__snap_to_grid";


ENVIL_AI_AGENT::ENVIL_AI_AGENT( KIWAY* aKiway, wxWindow* aParent, WEBVIEW_PANEL* aPanel ) :
        m_kiway( aKiway ),
        m_parent( aParent ),
        m_panel( aPanel ),
        m_approveAll( false ),
        m_busy( false )
{
    m_messages = json::array();
}


void ENVIL_AI_AGENT::Attach()
{
    if( !m_panel )
        return;

    m_panel->AddMessageHandler( wxS( "envilSend" ),
            [this]( const wxString& aMsg ) { onBridgeMessage( aMsg ); } );
}


void ENVIL_AI_AGENT::emit( const json& aMsg )
{
    // chat.html exposes window.appendServerMessage(jsonString). Build a JS call whose
    // argument is the JSON string; json(string).dump() yields a safe, escaped JS literal.
    std::string payload = aMsg.dump();
    wxString    script = wxS( "if(window.appendServerMessage)window.appendServerMessage(" )
                         + wxString::FromUTF8( json( payload ).dump() ) + wxS( ");" );

    WEBVIEW_PANEL* panel = m_panel;
    panel->CallAfter( [panel, script]() { panel->RunScriptAsync( script ); } );
}


void ENVIL_AI_AGENT::runOnUiSync( std::function<void()> aFn )
{
    wxSemaphore sem;

    m_panel->CallAfter( [&]()
    {
        aFn();
        sem.Post();
    } );

    sem.Wait();
}


void ENVIL_AI_AGENT::refreshContext()
{
    // Tell the model what it is looking at, so it behaves like an assistant that can see the
    // project rather than one that has to ask. Appended to the user-editable system prompt.
    wxString ctx;

    if( !m_projectPath.IsEmpty() )
        ctx << wxS( "\n\nCurrent project folder: " ) << m_projectPath;

    if( !m_schematicFile.IsEmpty() )
        ctx << wxS( "\nOpen schematic: " ) << m_schematicFile;

    m_client.SetContextSuffix( ctx );
}


void ENVIL_AI_AGENT::onBridgeMessage( const wxString& aJson )
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
        wxString newProject = wxString::FromUTF8( msg.value( "project_path", std::string() ) );
        wxString newSchematic = wxString::FromUTF8( msg.value( "schematic_file", std::string() ) );

        // Switching to a different schematic/project must start fresh so the previous
        // design's conversation doesn't carry over.
        if( !m_schematicFile.IsEmpty() && newSchematic != m_schematicFile )
        {
            m_messages = json::array();
            m_cliSession.Clear();
        }

        m_projectPath = newProject;
        m_schematicFile = newSchematic;
        refreshContext();
        emit( { { "kind", "ready" } } );
    }
    else if( kind == "ping" )
    {
        emit( { { "kind", "pong" } } );
    }
    else if( kind == "reset" )
    {
        if( m_busy )
            return;

        m_messages = json::array();
        m_cliSession.Clear();   // start a fresh CLI session on the next turn too
        emit( { { "kind", "status" }, { "text", "Conversation reset." } } );
    }
    else if( kind == "message" )
    {
        if( m_busy )
            return;

        wxString text = wxString::FromUTF8( msg.value( "text", std::string() ) );

        if( text.IsEmpty() )
            return;

        m_messages.push_back( { { "role", "user" },
                                { "content", std::string( text.utf8_str() ) } } );
        m_approveAll = false;
        m_busy = true;

        bool cli = useCliBackend();

        std::thread( [this, text, cli]()
                     {
                         if( cli )
                             agentLoopCli( text );
                         else
                             agentLoop( text );
                     } ).detach();
    }
    // approve/reject: placement is gated by a native dialog, so the webview's own
    // approve/reject flow is not used here.
}


std::string ENVIL_AI_AGENT::toolsJson() const
{
    // Mirrors the tool set exposed over MCP (tools/envil-mcp) so the in-app panel and an
    // external Claude client can do exactly the same things. All coords are mils, mult. of 50.
    const json xMils = { { "type", "integer" }, { "description", "X position in mils (mult. of 50)." } };
    const json yMils = { { "type", "integer" }, { "description", "Y position in mils (mult. of 50)." } };

    json tools = json::array();

    auto makeTool = []( const std::string& aName, const std::string& aDesc, const json& aProps,
                        const json& aRequired )
    {
        json schema;
        schema["type"] = "object";
        schema["properties"] = aProps;
        schema["required"] = aRequired;

        json t;
        t["name"] = aName;
        t["description"] = aDesc;
        t["input_schema"] = schema;
        return t;
    };

    tools.push_back( makeTool(
            "get_schematic",
            "Read the open schematic: each placed symbol with reference, value, lib_id, and "
            "every pin's number, name, and absolute [x_mils,y_mils]. Call before wiring so "
            "add_wire endpoints land on pins.",
            { { "include_pins",
                { { "type", "boolean" },
                  { "description", "Include pin lists (default true)." } } } },
            json::array() ) );

    tools.push_back( makeTool(
            "run_erc",
            "Run the native Electrical Rules Check and return violations (severity, title, "
            "message, x/y) + counts + a 'clean' flag. Use it to check your work and loop.",
            json::object(), json::array() ) );

    tools.push_back( makeTool(
            "snap_to_grid",
            "Bring off-grid symbol pins and wire ends onto the connection grid (default 50 "
            "mils), moving attached wire ends with their pins so connections survive. Use when "
            "ERC reports 'off connection grid' (common after an import).",
            { { "grid_mils",
                { { "type", "integer" },
                  { "description", "Connection grid in mils (default 50)." } } } },
            json::array() ) );

    tools.push_back( makeTool(
            "annotate",
            "Assign reference designators to un-annotated symbols. Run before run_erc.",
            json::object(), json::array() ) );

    tools.push_back( makeTool(
            "add_component", "Place a component symbol into the open schematic in Envil-CAD.",
            { { "lib_id",
                { { "type", "string" },
                  { "description", "KiCad library id 'Library:Symbol', e.g. "
                                   "'Regulator_Linear:AP2112K-3.3', 'Device:R', 'Device:C', "
                                   "'power:GND', 'power:+3V3'." } } },
              { "reference",
                { { "type", "string" }, { "description", "Reference designator, e.g. U1, R1." } } },
              { "value", { { "type", "string" }, { "description", "Optional value, e.g. '10k'." } } },
              { "x_mils", xMils },
              { "y_mils", yMils } },
            json::array( { "lib_id", "reference" } ) ) );

    tools.push_back( makeTool(
            "add_wire",
            "Draw a wire path; each consecutive pair of points becomes one segment. Use "
            "right-angle paths.",
            { { "points",
                { { "type", "array" },
                  { "description", "Ordered [x_mils, y_mils] pairs, at least two." },
                  { "items",
                    { { "type", "array" }, { "items", { { "type", "integer" } } } } } } } },
            json::array( { "points" } ) ) );

    tools.push_back( makeTool(
            "add_label", "Add a net label at a point (connect-by-name).",
            { { "name", { { "type", "string" }, { "description", "Net name, e.g. 'VOUT'." } } },
              { "kind",
                { { "type", "string" },
                  { "enum", json::array( { "label", "global", "hier" } ) },
                  { "description", "label = local (default), global, or hier." } } },
              { "x_mils", xMils },
              { "y_mils", yMils } },
            json::array( { "name" } ) ) );

    tools.push_back( makeTool( "add_junction",
                               "Add a junction dot where wires cross and must connect.",
                               { { "x_mils", xMils }, { "y_mils", yMils } },
                               json::array( { "x_mils", "y_mils" } ) ) );

    tools.push_back( makeTool( "add_no_connect", "Mark a pin as intentionally unconnected.",
                               { { "x_mils", xMils }, { "y_mils", yMils } },
                               json::array( { "x_mils", "y_mils" } ) ) );

    tools.push_back( makeTool(
            "edit_value", "Change the value field of an already-placed symbol.",
            { { "reference", { { "type", "string" }, { "description", "e.g. R2." } } },
              { "new_value", { { "type", "string" }, { "description", "e.g. '47k'." } } } },
            json::array( { "reference", "new_value" } ) ) );

    tools.push_back( makeTool(
            "move_component", "Move an already-placed symbol.",
            { { "reference", { { "type", "string" }, { "description", "e.g. C3." } } },
              { "x_mils", xMils },
              { "y_mils", yMils } },
            json::array( { "reference", "x_mils", "y_mils" } ) ) );

    tools.push_back( makeTool(
            "delete_component", "Delete a placed symbol by reference designator.",
            { { "reference", { { "type", "string" }, { "description", "e.g. R5." } } } },
            json::array( { "reference" } ) ) );

    tools.push_back( makeTool(
            "delete_at",
            "Delete stray wire/label/junction/no-connect at/near a point. Cleans orphan "
            "labels or dangling wire ends that ERC flags.",
            { { "x_mils", xMils },
              { "y_mils", yMils },
              { "radius_mils",
                { { "type", "integer" }, { "description", "Search radius in mils (default 30)." } } } },
            json::array( { "x_mils", "y_mils" } ) ) );

    return tools.dump();
}


bool ENVIL_AI_AGENT::approve( const wxString& aToolName, const wxString& aInputJson )
{
    if( m_approveAll )
        return true;

    bool approved = false;

    runOnUiSync( [&]()
    {
        wxString msg = wxString::Format(
                _( "Envil AI wants to run '%s' with:\n\n%s\n\nApply this change to your "
                   "schematic?" ),
                aToolName, aInputJson );

        wxMessageDialog dlg( m_parent, msg, _( "Approve Envil AI action" ),
                             wxYES_NO | wxCANCEL | wxICON_QUESTION );
        dlg.SetYesNoCancelLabels( _( "Approve" ), _( "Reject" ),
                                  _( "Approve all in this request" ) );

        int r = dlg.ShowModal();

        if( r == wxID_CANCEL )
        {
            m_approveAll = true;
            approved = true;
        }
        else
        {
            approved = ( r == wxID_YES );
        }
    } );

    return approved;
}


std::string ENVIL_AI_AGENT::execTool( const wxString& aToolName, const std::string& aInputJson,
                                      bool& aIsError )
{
    std::string message;
    bool        isError = true;

    runOnUiSync( [&]()
    {
        try
        {
            json req;
            req["tool"] = std::string( aToolName.utf8_str() );
            req["input"] = json::parse( aInputJson );

            std::string resultJson = EnvilSendSchematicTool( m_kiway, m_parent, req.dump() );

            json result = json::parse( resultJson );
            isError = !result.value( "ok", false );
            message = result.value( "message", std::string( "Tool returned no message." ) );
        }
        catch( const std::exception& e )
        {
            message = std::string( "Tool call failed: " ) + e.what();
        }
    } );

    aIsError = isError;
    return message;
}


void ENVIL_AI_AGENT::agentLoop( wxString aUserText )
{
    const std::string tools = toolsJson();

    emit( { { "kind", "status" }, { "text", "Analyzing" } } );

    for( int iter = 0; iter < 16; ++iter )
    {
        std::string assistantContent, stopReason;
        wxString    error;

        bool ok = m_client.SendTurn( m_messages.dump(), tools, assistantContent, stopReason,
                                     error );

        if( !ok )
        {
            emit( { { "kind", "error" }, { "text", std::string( error.utf8_str() ) } } );
            break;
        }

        json content;

        try
        {
            content = json::parse( assistantContent );
        }
        catch( const std::exception& )
        {
            break;
        }

        m_messages.push_back( { { "role", "assistant" }, { "content", content } } );

        for( const json& block : content )
        {
            if( block.value( "type", std::string() ) == "text" )
                emit( { { "kind", "reply" }, { "text", block.value( "text", std::string() ) } } );
        }

        json toolResults = json::array();
        bool hadToolUse = false;

        // Answer tool_use blocks whenever they are present rather than trusting
        // stop_reason: a max_tokens truncation still leaves them orphaned, and the API
        // rejects the next turn if any tool_use has no matching tool_result.
        for( const json& block : content )
        {
            if( block.value( "type", std::string() ) != "tool_use" )
                continue;

            hadToolUse = true;

            std::string id = block.value( "id", std::string() );
            wxString    name = wxString::FromUTF8( block.value( "name", std::string() ) );
            std::string inputJson = block.contains( "input" ) ? block["input"].dump() : "{}";

            bool        approved = approve( name, wxString::FromUTF8( inputJson ) );
            std::string resultText;
            bool        isError = false;

            if( !approved )
            {
                resultText = "User rejected this action.";
                isError = true;
            }
            else
            {
                resultText = execTool( name, inputJson, isError );
            }

            if( isError )
            {
                emit( { { "kind", "error" }, { "text", resultText } } );
            }
            else
            {
                emit( { { "kind", "applied" },
                        { "count", 1 },
                        { "total", 1 },
                        { "results", json::array( { json{ { "ok", true },
                                                          { "message", resultText } } } ) } } );
            }

            json tr;
            tr["type"] = "tool_result";
            tr["tool_use_id"] = id;
            tr["content"] = resultText;

            if( isError )
                tr["is_error"] = true;

            toolResults.push_back( tr );
        }

        if( !hadToolUse )
            break;

        m_messages.push_back( { { "role", "user" }, { "content", toolResults } } );
    }

    m_busy = false;
}


// ---------------------------------------------------------------------------------------
// CLI (subscription) backend
// ---------------------------------------------------------------------------------------

bool ENVIL_AI_AGENT::useCliBackend() const
{
    wxString v;

    if( wxGetEnv( wxS( "ENVIL_AI_BACKEND" ), &v ) )
    {
        wxString lv = v.Lower();

        if( lv == wxS( "api" ) )
            return false;

        if( lv == wxS( "cli" ) )
            return true;
    }

    return true;   // default: drive the Claude Code CLI on the user's subscription
}


wxString ENVIL_AI_AGENT::resolveClaudeExe() const
{
    wxString v;

    if( wxGetEnv( wxS( "ENVIL_CLAUDE_EXE" ), &v ) && !v.IsEmpty() )
        return v;

    // npm global install layout: %APPDATA%/npm/node_modules/@anthropic-ai/claude-code/bin/claude.exe
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

    return wxS( "claude" );   // last resort: rely on PATH
}


wxString ENVIL_AI_AGENT::writeCliMcpConfig() const
{
    wxString node;

    if( !wxGetEnv( wxS( "ENVIL_NODE_EXE" ), &node ) || node.IsEmpty() )
        node = wxS( "C:/Program Files/nodejs/node.exe" );

    wxString script;

    if( !wxGetEnv( wxS( "ENVIL_MCP_SCRIPT" ), &script ) || script.IsEmpty() )
        script = wxS( "D:/Ki_Cad_Full/Envil-CAD/tools/envil-mcp/index.mjs" );

    json cfg;
    cfg["mcpServers"]["envil-cad"]["command"] = std::string( node.utf8_str() );
    cfg["mcpServers"]["envil-cad"]["args"] = json::array( { std::string( script.utf8_str() ) } );

    wxFileName fn( SETTINGS_MANAGER::GetUserSettingsPath(), wxS( "envil_mcp.json" ) );
    wxFFile    f( fn.GetFullPath(), wxS( "wb" ) );

    if( f.IsOpened() )
    {
        f.Write( wxString::FromUTF8( cfg.dump( 2 ) ), wxConvUTF8 );
        f.Close();
    }

    return fn.GetFullPath();
}


void ENVIL_AI_AGENT::agentLoopCli( wxString aUserText )
{
    emit( { { "kind", "status" }, { "text", "Analyzing" } } );

#ifdef _WIN32
    wxString claudeExe = resolveClaudeExe();

    if( claudeExe != wxS( "claude" ) && !wxFileExists( claudeExe ) )
    {
        emit( { { "kind", "error" },
                { "text", "Claude CLI not found. Install it with: "
                          "npm install -g @anthropic-ai/claude-code" } } );
        m_busy = false;
        return;
    }

    const wxString cfgDir = SETTINGS_MANAGER::GetUserSettingsPath();
    wxString       mcpCfg = writeCliMcpConfig();

    // System prompt + user prompt go through files so nothing needs shell escaping.
    wxFileName sysFn( cfgDir, wxS( "envil_ai_sysprompt.tmp" ) );
    {
        wxFFile f( sysFn.GetFullPath(), wxS( "wb" ) );

        if( f.IsOpened() )
        {
            f.Write( m_client.GetSystemPrompt(), wxConvUTF8 );
            f.Close();
        }
    }

    wxFileName promptFn( cfgDir, wxS( "envil_ai_prompt_in.tmp" ) );
    {
        wxFFile f( promptFn.GetFullPath(), wxS( "wb" ) );

        if( f.IsOpened() )
        {
            f.Write( aUserText, wxConvUTF8 );
            f.Close();
        }
    }

    // Build the command line. Only fixed flags + our own paths/session id — no user text
    // (that arrives on stdin), so there is nothing to escape.
    wxString cmd;
    cmd << wxS( "\"" ) << claudeExe << wxS( "\"" )
        << wxS( " -p --output-format stream-json --verbose" )
        << wxS( " --mcp-config \"" ) << mcpCfg << wxS( "\"" )
        << wxS( " --append-system-prompt-file \"" ) << sysFn.GetFullPath() << wxS( "\"" )
        << wxS( " --allowedTools " ) << wxString::FromUTF8( ENVIL_MCP_ALLOWED_TOOLS );

    if( !m_cliSession.IsEmpty() )
        cmd << wxS( " --resume " ) << m_cliSession;

    // --- Spawn claude.exe with stdin = prompt file, stdout = pipe, no console window ---
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof( sa );
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hOutRd = nullptr, hOutWr = nullptr;

    if( !CreatePipe( &hOutRd, &hOutWr, &sa, 0 ) )
    {
        emit( { { "kind", "error" }, { "text", "Could not create output pipe for Claude CLI." } } );
        m_busy = false;
        return;
    }

    SetHandleInformation( hOutRd, HANDLE_FLAG_INHERIT, 0 );   // parent read end stays private

    HANDLE hIn = CreateFileW( promptFn.GetFullPath().wc_str(), GENERIC_READ, FILE_SHARE_READ, &sa,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

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
    cmdLine.push_back( L'\0' );   // CreateProcessW may modify the buffer

    BOOL okSpawn = CreateProcessW( nullptr, &cmdLine[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                   nullptr, nullptr, &si, &pi );

    CloseHandle( hOutWr );   // parent doesn't write
    if( hIn != INVALID_HANDLE_VALUE )
        CloseHandle( hIn );

    if( !okSpawn )
    {
        CloseHandle( hOutRd );
        emit( { { "kind", "error" }, { "text", "Failed to launch the Claude CLI process." } } );
        m_busy = false;
        return;
    }

    // Read stdout, split into newline-delimited stream-json events, dispatch each.
    std::string accum;
    char        buf[8192];
    DWORD       nRead = 0;

    while( ReadFile( hOutRd, buf, sizeof( buf ), &nRead, nullptr ) && nRead > 0 )
    {
        accum.append( buf, nRead );

        size_t nl;

        while( ( nl = accum.find( '\n' ) ) != std::string::npos )
        {
            std::string line = accum.substr( 0, nl );
            accum.erase( 0, nl + 1 );
            handleCliEvent( line );
        }
    }

    if( !accum.empty() )
        handleCliEvent( accum );

    WaitForSingleObject( pi.hProcess, INFINITE );
    CloseHandle( hOutRd );
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
#else
    emit( { { "kind", "error" },
            { "text", "The CLI backend is currently Windows-only. Set ENVIL_AI_BACKEND=api "
                      "to use an API key instead." } } );
#endif

    m_busy = false;
}


void ENVIL_AI_AGENT::handleCliEvent( const std::string& aLine )
{
    if( aLine.empty() )
        return;

    json j;

    try
    {
        j = json::parse( aLine );
    }
    catch( const std::exception& )
    {
        return;   // non-JSON noise (shouldn't happen with stream-json)
    }

    std::string type = j.value( "type", std::string() );

    if( type == "system" )
    {
        if( j.value( "subtype", std::string() ) == "init" && j.contains( "session_id" ) )
            m_cliSession = wxString::FromUTF8( j["session_id"].get<std::string>() );
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
                        emit( { { "kind", "reply" }, { "text", t } } );
                }
                else if( bt == "tool_use" )
                {
                    // Strip the mcp__envil-cad__ prefix for a readable status line.
                    std::string name = block.value( "name", std::string() );
                    size_t      p = name.rfind( "__" );

                    if( p != std::string::npos )
                        name = name.substr( p + 2 );

                    emit( { { "kind", "status" }, { "text", std::string( "Running " ) + name } } );
                }
            }
        }
    }
    else if( type == "result" )
    {
        if( j.contains( "session_id" ) )
            m_cliSession = wxString::FromUTF8( j["session_id"].get<std::string>() );

        // The final assistant text was already emitted via "assistant" events; only surface
        // an explicit error here.
        if( j.value( "is_error", false ) )
        {
            std::string msg = j.value( "result", std::string( "Claude CLI reported an error." ) );
            emit( { { "kind", "error" }, { "text", msg } } );
        }
    }
}
