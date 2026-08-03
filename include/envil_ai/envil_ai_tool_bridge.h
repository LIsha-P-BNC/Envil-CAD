/*
 * Envil AI — tool bridge.
 *
 * One place that turns an Envil AI tool request into a result by round-tripping it through
 * KIWAY to the editor that owns the document — the schematic editor (MAIL_ENVIL_AI_TOOL) or
 * the board editor (MAIL_ENVIL_PCB_TOOL). Both the in-app agent and the MCP tool socket use
 * this, so the cross-module call lives in exactly one spot.
 *
 * Must be called on the GUI thread: KIWAY dispatch reaches the editor frame, which touches
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

/**
 * Run one Envil AI tool call against the open board.
 *
 * @param aKiway      routes the request to the live board editor (FRAME_PCB_EDITOR).
 * @param aSource     source window for the mail event (may be null).
 * @param aRequestJson {"tool":"add_track","input":{...}}
 * @return {"ok":true|false,"message":"..."} — an ok:false JSON if no editor is open.
 */
KICOMMON_API std::string EnvilSendBoardTool( KIWAY* aKiway, wxWindow* aSource,
                                             const std::string& aRequestJson );

/**
 * Run one Envil AI tool call, routing it to the schematic or the board by tool name.
 *
 * Callers (the AI panel's agent, the MCP socket) hand over whatever the model asked for and
 * don't have to know which document each tool belongs to.
 */
KICOMMON_API std::string EnvilSendTool( KIWAY* aKiway, wxWindow* aSource,
                                        const std::string& aRequestJson );

/// True if @a aToolName is one of the board tools, i.e. belongs to FRAME_PCB_EDITOR.
KICOMMON_API bool EnvilIsBoardTool( const std::string& aToolName );

#endif // ENVIL_AI_TOOL_BRIDGE_H
