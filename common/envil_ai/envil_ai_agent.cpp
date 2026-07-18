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

using json = nlohmann::json;


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
        m_projectPath = wxString::FromUTF8( msg.value( "project_path", std::string() ) );
        m_schematicFile = wxString::FromUTF8( msg.value( "schematic_file", std::string() ) );
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

        std::thread( [this, text]() { agentLoop( text ); } ).detach();
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
