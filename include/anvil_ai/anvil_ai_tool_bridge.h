/*
 * Anvil AI — tool bridge.
 *
 * Routes one live AI tool request (a JSON string) to the editor that owns it, over KIWAY
 * mail. Dispatch is synchronous and the payload is carried by reference, so the editor's
 * handler writes its result straight back into the request string before these calls
 * return. Callable from any code that owns a KIWAY pointer (the shell's tool server, an
 * editor-owned agent) — it never needs the editor frame types themselves, which keeps it
 * usable on both sides of the KIFACE boundary.
 */

#ifndef ANVIL_AI_TOOL_BRIDGE_H
#define ANVIL_AI_TOOL_BRIDGE_H

#include <string>

#include <kicommon.h>       // KICOMMON_API: these live in kicommon.dll but are called from
                            // the shell (kicad.exe), so they must be exported.

class KIWAY;
class wxWindow;

/// Send one tool request to the schematic editor. Returns the result JSON
/// ({"ok":...,"message":...}); a closed editor yields ok:false with a helpful message.
KICOMMON_API std::string AnvilSendSchematicTool( KIWAY* aKiway, wxWindow* aSource,
                                                 const std::string& aRequestJson );

/// Send one tool request to the board editor. Same contract as the schematic variant.
KICOMMON_API std::string AnvilSendBoardTool( KIWAY* aKiway, wxWindow* aSource,
                                             const std::string& aRequestJson );

/// True when the named tool operates on the board rather than the schematic.
KICOMMON_API bool AnvilIsBoardTool( const std::string& aToolName );

/// Parse the request's "tool" field and route to the right editor.
KICOMMON_API std::string AnvilSendTool( KIWAY* aKiway, wxWindow* aSource,
                                        const std::string& aRequestJson );

#endif // ANVIL_AI_TOOL_BRIDGE_H
