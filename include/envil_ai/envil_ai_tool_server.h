/*
 * Envil AI — MCP tool socket server.
 *
 * A tiny line-delimited JSON server on 127.0.0.1 that lets an external process (the
 * envil-mcp Node bridge, driven by the user's own Claude client) run tools against the open
 * schematic. Each request is one line of {"tool":...,"input":{...}}; the reply is one line
 * of {"ok":...,"message":...}. Requests are executed via EnvilSendSchematicTool, so an
 * MCP client places parts through exactly the same path as the in-app AI panel.
 *
 * Modeled on KIWAY_PLAYER's DDE server: wxSocket events fire on the GUI event loop, so
 * handling is already on the UI thread that the KIWAY round-trip requires. Not exported;
 * compiled into the shell (kicad.exe) only, which is the process that owns the KIWAY.
 */

#ifndef ENVIL_AI_TOOL_SERVER_H
#define ENVIL_AI_TOOL_SERVER_H

#include <string>
#include <vector>

#include <wx/event.h>

class KIWAY;
class wxWindow;
class wxSocketServer;
class wxSocketBase;
class wxSocketEvent;

/// Default loopback port; override with the ENVIL_MCP_PORT environment variable.
#define ENVIL_MCP_DEFAULT_PORT 5571


class ENVIL_AI_TOOL_SERVER : public wxEvtHandler
{
public:
    /**
     * @param aKiway  routes tool calls to the schematic editor.
     * @param aParent source window for mail events (the shell frame).
     */
    ENVIL_AI_TOOL_SERVER( KIWAY* aKiway, wxWindow* aParent );
    ~ENVIL_AI_TOOL_SERVER() override;

    /// Begin listening on 127.0.0.1. Returns false if the port could not be bound.
    /// A stopped server may be started again (AnvilCAD MCP menu Start/Stop).
    bool Start();

    /// Stop listening and drop all connected clients. Safe to call when not running.
    void Stop();

    /// True while the socket is bound and listening.
    bool IsRunning() const { return m_server != nullptr; }

    /// Loopback port actually in use.
    int GetPort() const { return m_port; }

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
};

#endif // ENVIL_AI_TOOL_SERVER_H
