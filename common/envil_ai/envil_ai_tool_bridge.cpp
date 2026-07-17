/*
 * Envil AI — tool bridge. See envil_ai_tool_bridge.h.
 */

#include <envil_ai/envil_ai_tool_bridge.h>

#include <kiway.h>
#include <kiway_mail.h>
#include <mail_type.h>
#include <frame_type.h>


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
