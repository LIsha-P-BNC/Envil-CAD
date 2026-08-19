/*
 * Anvil AI — board-side executor for AI tool calls.
 *
 * The PCB counterpart of eeschema's envil_ai_tool_exec: the agent (in-app panel or an
 * external MCP client) reaches this through MAIL_ANVIL_PCB_TOOL, so board work goes through
 * exactly the same request/response path as schematic work.
 *
 * Declared without json/pcbnew types so cross-probing.cpp can call it without pulling
 * nlohmann in.
 */

#ifndef ANVIL_PCB_TOOL_EXEC_H
#define ANVIL_PCB_TOOL_EXEC_H

#include <string>

class PCB_EDIT_FRAME;

/**
 * Execute one Anvil AI board tool. Must run on the UI thread.
 *
 * @param aFrame the board editor to act on.
 * @param aRequestJson {"tool":"add_track","input":{...}}
 * @return {"ok":true|false,"message":"..."} — never throws; failures come back as ok:false.
 */
std::string AnvilExecPcbTool( PCB_EDIT_FRAME* aFrame, const std::string& aRequestJson );

/**
 * Write the board's footprints to a project-local <project>.pretty library and register it
 * in the project footprint-library table, so imported boards stop reporting missing
 * footprint libraries and their footprints become editable.
 *
 * @return "OK <n> footprints" or "ERROR <message>".
 */
std::string EnvilCaptureBoardFootprints( PCB_EDIT_FRAME* aFrame );

#endif // ANVIL_PCB_TOOL_EXEC_H
