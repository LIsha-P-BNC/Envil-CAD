/*
 * Anvil AI — native chat agent.
 *
 * Bridges the Anvil AI chat page (chat.html in a WEBVIEW_PANEL) to the Claude Agent SDK
 * engine: each turn spawns `claude.exe -p` (headless Claude Code on the user's own login)
 * with ONE MCP server — the SKiDL server (skidl_mcp_server.py) — which provides both
 * circuit generation (build / create_pcb / ...) and live editor tools (read_live /
 * edit_schematic_live / edit_board_live / check_live) relayed to this app's
 * ANVIL_AI_TOOL_SERVER on 127.0.0.1:5571.
 *
 * Deliberately module-independent so it can be owned either by an editor frame or by the
 * project-manager shell (CommonAiPanel). It never touches SCH_EDIT_FRAME — kicad.exe cannot
 * see that type across the KIFACE boundary — tool calls travel over KIWAY mail instead.
 *
 * All external locations are DISCOVERED, never hardcoded:
 *   claude.exe : ANVIL_CLAUDE_EXE env -> %APPDATA%\npm global install -> PATH
 *   python     : ANVIL_AI_PYTHON env -> %LOCALAPPDATA%\Programs\Python\Python3xx -> py -> python
 *   MCP server : ANVIL_MCP_SCRIPT env -> <stock data>/ai/skidl_mcp_server.py
 *                -> nearest ancestor of the exe containing skidl/skidl_mcp_server.py
 */

#ifndef ANVIL_AI_AGENT_H
#define ANVIL_AI_AGENT_H

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include <wx/arrstr.h>
#include <wx/string.h>

#include <json_common.h>

class KIWAY;
class WEBVIEW_PANEL;
class wxWindow;


// Not exported: compiled into each consumer (kicad.exe, _eeschema) rather than kicommon,
// because it uses WEBVIEW_PANEL from `common`, which links kicommon.
class ANVIL_AI_AGENT
{
public:
    /**
     * @param aKiway   routes live editor tool calls to the open editors.
     * @param aParent  the frame that owns the panel (mail-event source).
     * @param aPanel   the webview hosting chat.html.
     */
    ANVIL_AI_AGENT( KIWAY* aKiway, wxWindow* aParent, WEBVIEW_PANEL* aPanel );

    ~ANVIL_AI_AGENT();

    /// Register the JS->C++ bridge handler ("anvilSend") on the webview. Call before the
    /// page loads.
    void Attach();

    /// Tell the agent (and the page) what document is in front of the user. Any argument
    /// may be empty. Switching projects resets the conversation.
    void SetDocumentContext( const wxString& aProjectPath, const wxString& aSchematicFile,
                             const wxString& aBoardFile );

    /// Forward "the AI pane was just opened" to the page so it can focus its input.
    void NotifyPanelOpened();

private:
    // ---- bridge ------------------------------------------------------------------------
    void onBridgeMessage( const wxString& aJson );      // UI thread
    void emit( const nlohmann::json& aMsg );            // any thread -> page (marshalled)
    void pushContextToPage();                           // UI thread
    /// Decode chat attachments (data: URLs) to <settings>/anvil_attachments/ and return
    /// the saved file paths, so the model can read the images / PDFs / reports.
    wxArrayString saveAttachments( const nlohmann::json& aAtts );

    // ---- turn lifecycle ----------------------------------------------------------------
    void runTurn( wxString aUserText );                 // worker thread
    bool handleStreamEvent( const std::string& aLine ); // false = not JSON (CLI stderr)
    void setPhase( const wxString& aPhase );
    void startHeartbeat( const wxString& aPhase );
    void stopHeartbeat();
    long elapsedSeconds() const;
    void endTurn( const std::string& aReason, const wxString& aErrText = wxEmptyString );
    void cancelTurn();
    void killChild();

    // ---- discovery / config ------------------------------------------------------------
    wxString resolveClaudeExe() const;
    wxString resolvePython() const;
    wxString resolveServerScript() const;
    /// Write <settings>/anvil_mcp.json ({"mcpServers":{"anvil-cad":{...}}}); returns its
    /// path, empty on failure (missing python/script — the caller reports it).
    wxString writeMcpConfig( wxString* aWhatIsMissing );
    /// The user-editable base prompt (<settings>/anvil_ai_prompt.txt, seeded on first use)
    /// plus the live document context, written to a per-turn tmp file for
    /// --append-system-prompt-file.
    wxString writeSystemPromptFile();

    KIWAY*          m_kiway;
    wxWindow*       m_parent;
    WEBVIEW_PANEL*  m_panel;

    wxString        m_projectPath;
    wxString        m_schematicFile;
    wxString        m_boardFile;

    wxString        m_session;          // claude CLI session id (--resume)
    wxArrayString   m_sessionAttachments;  // files attached this conversation (re-listed each turn)

    std::atomic<bool> m_busy;
    std::atomic<bool> m_cancel;
    std::atomic<bool> m_sawReply;
    std::atomic<bool> m_turnClosed;

    // liveness ticker: the model can be silent for minutes, so the panel's "working" state
    // rides on these ticks, not on traffic
    std::thread       m_heartbeat;
    std::atomic<bool> m_heartbeatRun;
    std::mutex        m_hbMutex;
    wxString          m_phase;
    std::mutex        m_phaseMutex;
    std::chrono::steady_clock::time_point m_turnStart;

    void*           m_child;            // HANDLE of the running claude.exe (Windows)
    std::mutex      m_childMutex;
};

#endif // ANVIL_AI_AGENT_H
