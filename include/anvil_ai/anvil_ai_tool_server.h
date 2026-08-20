/*
 * Anvil AI — live tool socket server.
 *
 * A tiny line-delimited JSON server on 127.0.0.1 that lets an external process (the SKiDL
 * MCP server's live tools, driven by the in-app Anvil AI chat or the user's own Claude
 * client) run tools against the open editors. Each request is one line of
 * {"tool":...,"input":{...}}; the reply is one line of {"ok":...,"message":...}. Requests
 * execute via AnvilSendTool, so an external client edits the design through exactly the
 * same path as the in-app AI panel.
 *
 * Modeled on KIWAY_PLAYER's DDE server: wxSocket events fire on the GUI event loop, so
 * handling is already on the UI thread that the KIWAY round-trip requires. Not exported;
 * compiled into the shell (the process that owns the KIWAY). The AnvilCAD MCP menu's
 * Start/Stop drives Start()/Stop().
 */

#ifndef ANVIL_AI_TOOL_SERVER_H
#define ANVIL_AI_TOOL_SERVER_H

#include <functional>
#include <string>
#include <vector>

#include <wx/event.h>

class KIWAY;
class wxWindow;
class wxSocketServer;
class wxSocketBase;
class wxSocketEvent;

/// Default loopback port; override with the ANVIL_MCP_PORT environment variable.
#define ANVIL_MCP_DEFAULT_PORT 5571


class ANVIL_AI_TOOL_SERVER : public wxEvtHandler
{
public:
    /**
     * @param aKiway  routes tool calls to the open editors.
     * @param aParent source window for mail events (the shell frame).
     */
    ANVIL_AI_TOOL_SERVER( KIWAY* aKiway, wxWindow* aParent );
    ~ANVIL_AI_TOOL_SERVER() override;

    /// Begin listening on 127.0.0.1. Returns false if the port could not be bound.
    /// A stopped server may be started again (AnvilCAD MCP menu Start/Stop).
    bool Start();

    /// Stop listening and drop all connected clients. Safe to call when not running.
    void Stop();

    /// True while the socket is bound and listening.
    bool IsRunning() const { return m_server != nullptr; }

    /// Loopback port actually in use.
    int GetPort() const { return m_port; }

    /// Install a handler for shell-level tools (e.g. "open_project") that the editor tool
    /// bridge can't service. Given the raw request JSON, it returns a result JSON string,
    /// or an EMPTY string to mean "not a shell tool — fall through to the editors".
    void SetShellHandler( std::function<std::string( const std::string& )> aHandler )
    {
        m_shellHandler = std::move( aHandler );
    }

private:
    void onServerEvent( wxSocketEvent& aEvent );   // incoming connection
    void onClientEvent( wxSocketEvent& aEvent );   // data / disconnect
    void handleLine( wxSocketBase* aSock, const std::string& aLine );

    KIWAY*                      m_kiway;
    wxWindow*                   m_parent;
    int                         m_port;
    wxSocketServer*             m_server;
    std::vector<wxSocketBase*>  m_clients;
    std::string                 m_rxAccum;   // partial line buffer (one client at a time)
    std::function<std::string( const std::string& )> m_shellHandler;  // open_project etc.
};

#endif // ANVIL_AI_TOOL_SERVER_H
