/*
 * Envil AI — tool bridge.
 *
 * One place that turns an Envil AI tool request into a result by round-tripping it through
 * KIWAY to the schematic editor (MAIL_ENVIL_AI_TOOL). Both the in-app agent and the MCP
 * tool socket use this, so the cross-module call lives in exactly one spot.
 *
 * Must be called on the GUI thread: KIWAY dispatch reaches SCH_EDIT_FRAME, which touches
 * the canvas.
 */

#ifndef ENVIL_AI_TOOL_BRIDGE_H
#define ENVIL_AI_TOOL_BRIDGE_H

#include <string>
#include <kicommon.h>

class KIWAY;
class wxWindow;

/**
 * Run one Envil AI tool call against the open schematic.
 *
 * @param aKiway      routes the request to the live schematic editor (FRAME_SCH).
 * @param aSource     source window for the mail event (may be null).
 * @param aRequestJson {"tool":"add_component","input":{...}}
 * @return {"ok":true|false,"message":"..."} — an ok:false JSON if no editor is open.
 */
KICOMMON_API std::string EnvilSendSchematicTool( KIWAY* aKiway, wxWindow* aSource,
                                                 const std::string& aRequestJson );

#endif // ENVIL_AI_TOOL_BRIDGE_H
