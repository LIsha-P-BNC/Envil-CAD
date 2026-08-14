/*
 * Envil AI — native agent driving the Anvil chat UI.
 *
 * Bridges chat.html (running in a WEBVIEW_PANEL) to a native C++ Claude agent. No Python
 * backend: the webview talks to this class over the wxWebView JS<->C++ bridge, and this
 * class calls Claude's Messages API itself.
 *
 * Deliberately module-independent so it can be owned either by an editor frame or by the
 * project-manager shell (CommonAiPanel, Cursor style). It therefore never touches
 * SCH_EDIT_FRAME — kicad.exe cannot see that type across the KIFACE boundary — and instead
 * runs tool calls by sending MAIL_ENVIL_AI_TOOL over KIWAY, which dispatches synchronously
 * and hands the result straight back in the payload.
 */

#ifndef ENVIL_AI_AGENT_H
#define ENVIL_AI_AGENT_H

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <wx/string.h>

#include <json_common.h>
#include <envil_ai/envil_ai_client.h>

class KIWAY;
class WEBVIEW_PANEL;
class wxWindow;


// Not exported: compiled into each consumer (kicad.exe, _eeschema) rather than kicommon,
// because it uses WEBVIEW_PANEL from `common`, which links kicommon.
class ENVIL_AI_AGENT
{
public:
    /**
     * @param aKiway   used to route tool calls to the schematic editor.
     * @param aParent  parent for the approval dialog (the frame that owns the panel).
     * @param aPanel   the webview hosting chat.html.
     */
    ENVIL_AI_AGENT( KIWAY* aKiway, wxWindow* aParent, WEBVIEW_PANEL* aPanel );

    ~ENVIL_AI_AGENT();

    /// Register the JS->C++ bridge handler on the webview. Call before the page loads.
    void Attach();

private:
    void        onBridgeMessage( const wxString& aJson );   // UI thread
    void        agentLoop( wxString aUserText );            // worker thread — API-key backend
    void        agentLoopCli( wxString aUserText );         // worker thread — subscription (CLI)
    /// Parse one stream-json line. False = the line was not JSON (the CLI's own stderr),
    /// which the caller keeps so a failed run can explain itself instead of going quiet.
    bool        handleCliEvent( const std::string& aLine );
    void        emit( const nlohmann::json& aMsg );         // push to the webview
    void        runOnUiSync( std::function<void()> aFn );

    // ---- turn lifecycle / liveness ------------------------------------------------------
    //
    // A turn can run for minutes with the model saying nothing at all (a big prompt, a long
    // thinking block, one slow ERC/DRC call). The panel therefore cannot infer "still alive"
    // from traffic: the agent has to keep saying so. These drive that contract — every turn
    // opens with a phase, ticks a heartbeat while it runs, and closes with exactly one
    // "done", whatever path it takes out (reply, error, cancel, or a dead CLI process).

    /// Set what we tell the user we are doing right now, and push it as a named step.
    void        setPhase( const wxString& aPhase );
    wxString    currentPhase();

    /// Start/stop the background ticker that emits {kind:"progress"} while a turn is live.
    void        startHeartbeat( const wxString& aPhase );
    void        stopHeartbeat();
    long        elapsedSeconds() const;

    /// Close the turn exactly once: stop the heartbeat, clear busy, emit {kind:"done"}.
    /// aReason is "ok", "cancelled" or "error".
    void        endTurn( const std::string& aReason, const wxString& aText = wxEmptyString );

    /// User pressed Stop. Flags the turn and kills the CLI process so the read loop unblocks.
    void        cancelTurn();
    void        killCliProcess();
    bool        approve( const wxString& aToolName, const wxString& aInputJson );
    std::string execTool( const wxString& aToolName, const std::string& aInputJson,
                          bool& aIsError );
    std::string toolsJson() const;
    void        refreshContext();

    /// Backend selection. "cli" = drive the Claude Code CLI on the user's subscription (no
    /// API key); "api" = call the Anthropic Messages API directly. From ENVIL_AI_BACKEND,
    /// defaulting to CLI.
    bool        useCliBackend() const;

    /// Locate claude.exe / the MCP script / node, and (re)write the CLI's MCP config file.
    wxString    resolveClaudeExe() const;
    wxString    writeCliMcpConfig() const;

    KIWAY*          m_kiway;
    wxWindow*       m_parent;
    WEBVIEW_PANEL*  m_panel;
    ENVIL_AI_CLIENT m_client;
    nlohmann::json  m_messages;
    wxString        m_projectPath;      // from chat.html's "hello"
    wxString        m_schematicFile;
    wxString        m_cliSession;       // CLI session id for --resume (multi-turn)
    bool            m_approveAll;

    // Written on a worker thread, read on the UI thread (and vice versa) — atomic, not bool.
    std::atomic<bool> m_busy;
    std::atomic<bool> m_cancel;
    std::atomic<bool> m_sawReply;    // did this turn produce any assistant text at all?
    std::atomic<bool> m_turnClosed;  // endTurn() is idempotent; this is the latch

    std::atomic<bool>                     m_heartbeatRun;
    std::thread                           m_heartbeat;
    std::mutex                            m_hbMutex;   // serialises start/stop + join
    std::chrono::steady_clock::time_point m_turnStart;

    std::mutex m_phaseMutex;
    wxString   m_phase;

    std::mutex m_procMutex;
    void*      m_cliProcess;         // Win32 HANDLE of the live claude.exe, or nullptr
};

#endif // ENVIL_AI_AGENT_H
