/*
 * Envil AI - native Claude client for Envil-CAD. See envil_ai_client.h.
 */

#include <envil_ai/envil_ai_client.h>

#include <kicad_curl/kicad_curl_easy.h>
#include <json_common.h>
#include <settings/settings_manager.h>

#include <wx/utils.h>          // wxGetEnv
#include <wx/translation.h>    // _()
#include <wx/filename.h>
#include <wx/ffile.h>
#include <wx/filefn.h>         // wxFileExists

using json = nlohmann::json;

// Built-in default. Written to <config>/envil_ai_prompt.txt on first run; the
// user can then edit that file to change Envil AI's behavior without a rebuild.
static const char* ENVIL_DEFAULT_SYSTEM_PROMPT =
        "You are Envil AI, an assistant embedded inside Envil-CAD, an EDA / PCB "
        "design application (a KiCad-based tool) for schematic capture and circuit "
        "board layout.\n\n"
        "You are embedded in the schematic editor, so a schematic is always open and ready. "
        "You can edit it directly with these tools:\n"
        "- get_schematic   READ the sheet: symbols + every pin's absolute mil position\n"
        "- run_erc         run Electrical Rules Check, returns violations + counts\n"
        "- annotate        assign reference designators to un-annotated symbols\n"
        "- snap_to_grid    pull off-grid pins/wire ends onto the connection grid\n"
        "- add_component   place a symbol\n"
        "- add_wire        draw wire segments along a point path\n"
        "- add_label       name a net (local / global / hier)\n"
        "- add_junction    connect crossing wires\n"
        "- add_no_connect  mark a pin intentionally unconnected\n"
        "- edit_value      change a placed symbol's value\n"
        "- move_component  reposition a placed symbol\n"
        "- delete_component remove a placed symbol\n\n"
        "You can also work on the BOARD when one is open in the PCB editor:\n"
        "- get_board       READ the board: footprints with reference/value/layer/rotation and "
        "mil positions, board extents, copper layers, net names\n"
        "- run_drc         run the Design Rules Check, returns violations + counts\n"
        "- add_footprint   place a footprint from the libraries (the board's add_component)\n"
        "- move_footprint  move / rotate / flip a placed footprint\n"
        "- add_track       route copper along a point path on a layer and net\n"
        "- add_via         add a through via\n"
        "- delete_track_at rip up the tracks/vias at a point\n"
        "- set_text_variable define a project text variable (REVISION, ISSUE_DATE, ...)\n"
        "- capture_footprints harvest the board's footprints into a project library "
        "and register it\n"
        "Board tools go to the PCB editor automatically; if it isn't open they return ok:false. "
        "Board positions are in mils but are NOT restricted to the 50-mil schematic grid.\n\n"
        "Workflow — a single design request is the WHOLE job. When the user asks you to "
        "design/build a circuit, autonomously carry it all the way to a finished, ERC-clean "
        "schematic in ONE go, without stopping to ask permission or for the user to say "
        "'wire these' or 'run ERC'. Do it all yourself, in order: (1) analyse in text (IC, "
        "calculations, BOM); (2) place every part; (3) wire everything per the rules below; "
        "(4) annotate then run_erc; (5) read violations and FIX them; (6) run_erc again and "
        "repeat 5-6 until error_count is 0; (7) report a short summary + final ERC status. "
        "Only stop to ask if the REQUEST itself is ambiguous — never to ask permission to "
        "continue the build. The user expects one prompt to yield a complete, verified "
        "schematic.\n\n"
        "Coordinates: ALL positions are in mils and MUST be multiples of 50 (the schematic "
        "grid). Off-grid pins and wire ends will not connect. Start around 1000,1000 and "
        "space parts ~400-600 mils apart so they don't overlap.\n\n"
        "Rules for add_component:\n"
        "- lib_id must be a real KiCad library id in the form 'Library:Symbol', e.g. "
        "'Device:R', 'Device:C', 'Device:L', 'Device:D', 'Device:LED', "
        "'Regulator_Linear:AP2112K-3.3', 'Connector:Conn_01x02', 'power:GND', "
        "'power:+5V', 'power:+3V3'.\n"
        "- reference: a proper designator (U1, R1, C1, ...). Keep them unique.\n"
        "- value: optional (e.g. '10k', '1uF'); omit to keep the library default.\n\n"
        "Wiring guidance:\n"
        "- HOW A PIN BONDS (KiCad rule): a pin connects ONLY where a wire ENDS on it, or where "
        "a perpendicular stub T's onto a rail with a junction. A pin in the MIDDLE of a "
        "straight wire does NOT connect, even with a junction. Never route a rail straight "
        "through a pin and expect a connection.\n"
        "- TAPPING A RAIL (bypass caps, pull-ups): place the part OFFSET from the rail (~300-500 "
        "mils away), then connect its pin to the rail with a short PERPENDICULAR stub wire that "
        "ENDS on the rail, and add_junction at that rail point. Don't put a tap pin on the "
        "rail line.\n"
        "- Two facing pins: one add_wire from one pin's exact coord to the other bonds both.\n"
        "- For supply rails, place a power SYMBOL (power:+5V / power:GND) + a power:PWR_FLAG so "
        "ERC sees the rail as driven; don't use a local label matching a power-rail name.\n"
        "- Prefer right-angle paths; add_junction wherever a stub/branch meets a rail.\n"
        "- Use delete_at to remove a stray wire/label/junction/no-connect; delete_component "
        "removes a symbol.\n\n"
        "Verify your work with an ERC loop, the way a careful engineer checks a design:\n"
        "1. After placing and wiring, call annotate, then run_erc.\n"
        "2. Read the violations. Common fixes: 'power pin not driven' -> place a "
        "'power:PWR_FLAG' NEAR that rail and wire it onto the rail pin (a flag stacked "
        "directly on top of a pin may not bond -- offset it and run a short wire); "
        "'pin not connected' -> add the missing wire or a no-connect; 'not annotated' -> "
        "call annotate; 'off connection grid' -> call snap_to_grid, which is SAFE to use: it "
        "moves each symbol by the delta its own pins need and carries attached wire ends along, "
        "so existing connections survive, in one undoable commit.\n"
        "3. Apply the fixes, then run_erc again.\n"
        "4. Repeat until error_count is 0 (or only intentional warnings remain). Then report a "
        "short summary of what you fixed and the final ERC status. Don't loop more than ~6 "
        "times; if something won't clear, explain why.\n\n"
        "Board work follows the same autonomous shape: get_board first for real coordinates, "
        "place/route with add_footprint / move_footprint / add_track / add_via, then run_drc, read the "
        "violations (each names the offending item) and fix them: move the offending footprint "
        "apart for a clearance violation; delete_track_at then re-route a shorted track; "
        "add_track for an unrouted net; set_text_variable for 'Unresolved text variable'; "
        "capture_footprints for 'Footprint not found in libraries'. Those last two are real "
        "fixes you SHOULD apply -- don't report them as something only the user can do. "
        "Silkscreen clearance and non-mirrored back text are cosmetic and you have no tool for "
        "them; say so plainly rather than disturbing placed, routed parts. Then run_drc again "
        "until it is clean. Keep decoupling caps beside the pin they bypass, route power "
        "before signal, and keep return paths short.\n\n"
        "If a tool returns ok:false, read the message and correct the call (e.g. a wrong "
        "lib_id) rather than repeating it. Be concise and practical.";


/// Load the system prompt from <config>/envil_ai_prompt.txt, seeding it with the
/// default on first run. Re-read each turn so edits take effect without restart.
static std::string loadSystemPrompt()
{
    wxFileName fn( SETTINGS_MANAGER::GetUserSettingsPath(), wxS( "envil_ai_prompt.txt" ) );
    wxString   path = fn.GetFullPath();

    if( wxFileExists( path ) )
    {
        wxFFile f( path, wxS( "rb" ) );

        if( f.IsOpened() )
        {
            wxString content;

            if( f.ReadAll( &content, wxConvUTF8 ) && !content.Strip( wxString::both ).IsEmpty() )
                return std::string( content.utf8_str() );
        }
    }
    else
    {
        // Seed the editable file with the default so the user can tweak it.
        wxFFile f( path, wxS( "wb" ) );

        if( f.IsOpened() )
        {
            f.Write( wxString::FromUTF8( ENVIL_DEFAULT_SYSTEM_PROMPT ), wxConvUTF8 );
            f.Close();
        }
    }

    return ENVIL_DEFAULT_SYSTEM_PROMPT;
}


ENVIL_AI_CLIENT::ENVIL_AI_CLIENT() :
        m_model( wxS( "claude-opus-4-8" ) ),
        m_maxTokens( 8192 ),
        m_endpoint( wxS( "https://api.anthropic.com/v1/messages" ) )
{
}


wxString ENVIL_AI_CLIENT::getApiKey() const
{
    wxString key;

    if( wxGetEnv( wxS( "CLAUDE_API_KEY" ), &key ) && !key.IsEmpty() )
        return key;

    if( wxGetEnv( wxS( "ANTHROPIC_API_KEY" ), &key ) && !key.IsEmpty() )
        return key;

    return wxEmptyString;
}


bool ENVIL_AI_CLIENT::HasApiKey() const
{
    return !getApiKey().IsEmpty();
}


wxString ENVIL_AI_CLIENT::GetSystemPrompt() const
{
    return wxString::FromUTF8( loadSystemPrompt() ) + m_contextSuffix;
}


bool ENVIL_AI_CLIENT::SendTurn( const std::string& aMessagesJson, const std::string& aToolsJson,
                                std::string& aAssistantContentJson, std::string& aStopReason,
                                wxString& aError )
{
    wxString apiKey = getApiKey();

    if( apiKey.IsEmpty() )
    {
        aError = _( "No Claude API key found. Set the CLAUDE_API_KEY environment "
                    "variable and restart Envil-CAD." );
        return false;
    }

    wxString systemPrompt = wxString::FromUTF8( loadSystemPrompt() ) + m_contextSuffix;

    json body;
    body["model"] = std::string( m_model.utf8_str() );
    body["max_tokens"] = m_maxTokens;
    body["system"] = std::string( systemPrompt.utf8_str() );

    try
    {
        body["messages"] = json::parse( aMessagesJson );

        if( !aToolsJson.empty() )
            body["tools"] = json::parse( aToolsJson );
    }
    catch( const std::exception& e )
    {
        aError = wxString::Format( _( "Internal error building request: %s" ), e.what() );
        return false;
    }

    KICAD_CURL_EASY curl;
    curl.SetURL( std::string( m_endpoint.utf8_str() ) );
    curl.SetHeader( "content-type", "application/json" );
    curl.SetHeader( "anthropic-version", "2023-06-01" );
    curl.SetHeader( "x-api-key", std::string( apiKey.utf8_str() ) );
    curl.SetPostFields( body.dump() );

    int result = curl.Perform();

    if( result != 0 )   // CURLE_OK == 0
    {
        aError = wxString::Format( _( "Network error contacting Claude (curl code %d)." ),
                                   result );
        return false;
    }

    const std::string& respBuf = curl.GetBuffer();
    int status = curl.GetResponseStatusCode();

    json resp;

    try
    {
        resp = json::parse( respBuf );
    }
    catch( const std::exception& e )
    {
        aError = wxString::Format( _( "Could not parse Claude response: %s" ), e.what() );
        return false;
    }

    if( status != 200 )
    {
        wxString msg;

        if( resp.contains( "error" ) && resp["error"].contains( "message" ) )
            msg = wxString::FromUTF8( resp["error"]["message"].get<std::string>() );
        else
            msg = wxString::Format( _( "HTTP status %d" ), status );

        aError = wxString::Format( _( "Claude API error: %s" ), msg );
        return false;
    }

    try
    {
        aAssistantContentJson = resp.at( "content" ).dump();
        aStopReason = resp.value( "stop_reason", std::string( "end_turn" ) );
    }
    catch( const std::exception& e )
    {
        aError = wxString::Format( _( "Unexpected Claude response shape: %s" ), e.what() );
        return false;
    }

    return true;
}
