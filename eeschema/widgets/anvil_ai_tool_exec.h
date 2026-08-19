/*
 * Anvil AI — schematic-side executor for AI tool calls.
 *
 * The AI agent may live in another module (the project-manager shell owns the chat panel
 * when CommonAiPanel is set, and kicad.exe cannot see SCH_EDIT_FRAME across the KIFACE
 * boundary). It reaches this code by sending MAIL_ANVIL_AI_TOOL, which SCH_EDIT_FRAME's
 * KiwayMailIn forwards here. Declared without json/eeschema types so cross-probing.cpp can
 * call it without pulling nlohmann in.
 */

#ifndef ANVIL_AI_TOOL_EXEC_H
#define ANVIL_AI_TOOL_EXEC_H

#include <string>

class SCH_EDIT_FRAME;

/**
 * Execute one Anvil AI tool call against the open schematic. Must run on the UI thread.
 *
 * @param aFrame the schematic editor to act on.
 * @param aRequestJson {"tool":"add_component","input":{...}}
 * @return {"ok":true|false,"message":"..."} — never throws; failures come back as ok:false.
 */
std::string AnvilExecAiTool( SCH_EDIT_FRAME* aFrame, const std::string& aRequestJson );

/**
 * Convert a symbol library (Altium .SchLib/.IntLib, Anvil .kicad_sym/.anvil_sym, legacy
 * .lib) to the native s-expression format under the given destination path.
 *
 * @param aRequest "srcPath\ndestPath"
 * @return "OK <n> symbols" or "ERROR <message>".
 */
std::string EnvilConvertSymbolLib( const std::string& aRequest );

#endif // ANVIL_AI_TOOL_EXEC_H
