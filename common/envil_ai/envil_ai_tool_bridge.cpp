/*
 * Envil AI — tool bridge. See envil_ai_tool_bridge.h.
 */

#include <envil_ai/envil_ai_tool_bridge.h>

#include <kiway.h>
#include <kiway_mail.h>
#include <mail_type.h>
#include <frame_type.h>
#include <json_common.h>

#include <set>


std::string EnvilSendSchematicTool( KIWAY* aKiway, wxWindow* aSource,
                                    const std::string& aRequestJson )
{
    // The payload is held BY REFERENCE by KIWAY_MAIL_EVENT and KIWAY::ProcessEvent
    // dispatches synchronously, so the schematic editor's KiwayMailIn writes its result
    // straight back into this local before ProcessEvent returns.
    std::string payload = aRequestJson;

    if( !aKiway )
        return R"({"ok":false,"message":"No KIWAY available."})";

    KIWAY_MAIL_EVENT mail( FRAME_SCH, MAIL_ENVIL_AI_TOOL, payload, aSource );

    if( !aKiway->ProcessEvent( mail ) )
    {
        return R"({"ok":false,"message":"No schematic editor is open. Open the Schematic )"
               R"(Editor and try again."})";
    }

    return payload;
}


std::string EnvilSendBoardTool( KIWAY* aKiway, wxWindow* aSource, const std::string& aRequestJson )
{
    std::string payload = aRequestJson;

    if( !aKiway )
        return R"({"ok":false,"message":"No KIWAY available."})";

    KIWAY_MAIL_EVENT mail( FRAME_PCB_EDITOR, MAIL_ENVIL_PCB_TOOL, payload, aSource );

    if( !aKiway->ProcessEvent( mail ) )
    {
        return R"({"ok":false,"message":"No board editor is open. Open the PCB Editor and )"
               R"(try again."})";
    }

    return payload;
}


bool EnvilIsBoardTool( const std::string& aToolName )
{
    static const std::set<std::string> boardTools = {
        "get_board", "run_drc", "add_footprint", "move_footprint", "add_track", "add_via",
        "delete_track_at", "set_text_variable", "capture_footprints"
    };

    return boardTools.count( aToolName ) > 0;
}


std::string EnvilSendTool( KIWAY* aKiway, wxWindow* aSource, const std::string& aRequestJson )
{
    std::string toolName;

    try
    {
        toolName = nlohmann::json::parse( aRequestJson ).value( "tool", std::string() );
    }
    catch( const std::exception& )
    {
        return R"({"ok":false,"message":"Tool request was not valid JSON."})";
    }

    if( EnvilIsBoardTool( toolName ) )
        return EnvilSendBoardTool( aKiway, aSource, aRequestJson );

    return EnvilSendSchematicTool( aKiway, aSource, aRequestJson );
}
