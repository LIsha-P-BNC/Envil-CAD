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

#include <functional>
#include <string>

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

    /// Register the JS->C++ bridge handler on the webview. Call before the page loads.
    void Attach();

private:
    void        onBridgeMessage( const wxString& aJson );   // UI thread
    void        agentLoop( wxString aUserText );            // worker thread — API-key backend
    void        agentLoopCli( wxString aUserText );         // worker thread — subscription (CLI)
    void        handleCliEvent( const std::string& aLine ); // parse one stream-json line
    void        emit( const nlohmann::json& aMsg );         // push to the webview
    void        runOnUiSync( std::function<void()> aFn );
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
    bool            m_busy;
};

#endif // ENVIL_AI_AGENT_H
