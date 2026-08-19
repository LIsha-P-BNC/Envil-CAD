/*
 * Anvil AI — live tool socket server. See anvil_ai_tool_server.h.
 */

#include <anvil_ai/anvil_ai_tool_server.h>
#include <anvil_ai/anvil_ai_tool_bridge.h>

#include <wx/socket.h>
#include <wx/sckaddr.h>
#include <wx/log.h>
#include <wx/utils.h>       // wxGetEnv
#include <algorithm>

enum
{
    ID_ANVIL_SOCK_SERVER = 5711,
    ID_ANVIL_SOCK_CLIENT
};


ANVIL_AI_TOOL_SERVER::ANVIL_AI_TOOL_SERVER( KIWAY* aKiway, wxWindow* aParent ) :
        m_kiway( aKiway ),
        m_parent( aParent ),
        m_port( ANVIL_MCP_DEFAULT_PORT ),
        m_server( nullptr )
{
    wxString portStr;

    if( wxGetEnv( wxS( "ANVIL_MCP_PORT" ), &portStr ) && !portStr.IsEmpty() )
    {
        long p = 0;

        if( portStr.ToLong( &p ) && p > 0 && p < 65536 )
            m_port = (int) p;
    }

    // Bound once for the object's lifetime so that repeated Start()/Stop() cycles (driven
    // by the AnvilCAD MCP menu) never stack duplicate handlers.
    Bind( wxEVT_SOCKET, &ANVIL_AI_TOOL_SERVER::onServerEvent, this, ID_ANVIL_SOCK_SERVER );
    Bind( wxEVT_SOCKET, &ANVIL_AI_TOOL_SERVER::onClientEvent, this, ID_ANVIL_SOCK_CLIENT );
}


ANVIL_AI_TOOL_SERVER::~ANVIL_AI_TOOL_SERVER()
{
    Stop();
}


void ANVIL_AI_TOOL_SERVER::Stop()
{
    for( wxSocketBase* sock : m_clients )
    {
        if( sock )
        {
            sock->Notify( false );
            sock->Destroy();
        }
    }

    m_clients.clear();
    m_rxAccum.clear();

    if( m_server )
    {
        m_server->Notify( false );
        m_server->Destroy();
        m_server = nullptr;

        wxLogTrace( wxS( "ANVIL" ), wxS( "Anvil AI tool server stopped" ) );
    }
}


bool ANVIL_AI_TOOL_SERVER::Start()
{
    if( m_server )
        return true;    // already listening

    wxIPV4address addr;
    addr.Hostname( wxS( "127.0.0.1" ) );        // loopback only — never exposed off-box
    addr.Service( m_port );

    m_server = new wxSocketServer( addr, wxSOCKET_NOWAIT );

    if( !m_server->IsOk() )
    {
        wxLogTrace( wxS( "ANVIL" ), wxS( "Anvil AI tool server: could not bind port %d" ),
                    m_port );
        m_server->Destroy();
        m_server = nullptr;
        return false;
    }

    m_server->SetEventHandler( *this, ID_ANVIL_SOCK_SERVER );
    m_server->SetNotify( wxSOCKET_CONNECTION_FLAG );
    m_server->Notify( true );

    wxLogTrace( wxS( "ANVIL" ), wxS( "Anvil AI tool server listening on 127.0.0.1:%d" ),
                m_port );
    return true;
}


void ANVIL_AI_TOOL_SERVER::onServerEvent( wxSocketEvent& aEvent )
{
    if( aEvent.GetSocketEvent() != wxSOCKET_CONNECTION )
        return;

    wxSocketBase* sock = m_server->Accept( false );

    if( !sock )
        return;

    m_clients.push_back( sock );
    m_rxAccum.clear();

    sock->SetEventHandler( *this, ID_ANVIL_SOCK_CLIENT );
    sock->SetNotify( wxSOCKET_INPUT_FLAG | wxSOCKET_LOST_FLAG );
    sock->Notify( true );
}


void ANVIL_AI_TOOL_SERVER::onClientEvent( wxSocketEvent& aEvent )
{
    wxSocketBase* sock = aEvent.GetSocket();

    switch( aEvent.GetSocketEvent() )
    {
    case wxSOCKET_INPUT:
    {
        char buf[4096];

        // Drain everything available; wxSOCKET_NOWAIT means Read returns what's ready.
        for( ;; )
        {
            sock->Read( buf, sizeof( buf ) );
            size_t n = sock->LastCount();

            if( n == 0 )
                break;

            m_rxAccum.append( buf, n );
        }

        // Dispatch each complete newline-terminated request.
        size_t nl;

        while( ( nl = m_rxAccum.find( '\n' ) ) != std::string::npos )
        {
            std::string line = m_rxAccum.substr( 0, nl );
            m_rxAccum.erase( 0, nl + 1 );

            if( !line.empty() && line.back() == '\r' )
                line.pop_back();

            if( !line.empty() )
                handleLine( sock, line );
        }

        break;
    }

    case wxSOCKET_LOST:
        sock->Notify( false );
        m_clients.erase( std::remove( m_clients.begin(), m_clients.end(), sock ),
                         m_clients.end() );
        sock->Destroy();
        break;

    default:
        break;
    }
}


void ANVIL_AI_TOOL_SERVER::handleLine( wxSocketBase* aSock, const std::string& aLine )
{
    // We are already on the GUI thread (wxSocket events dispatch on the event loop), so the
    // KIWAY round-trip into the editor is safe to run directly.
    std::string result = AnvilSendTool( m_kiway, m_parent, aLine );
    result.push_back( '\n' );

    aSock->Write( result.data(), (wxUint32) result.size() );
}
