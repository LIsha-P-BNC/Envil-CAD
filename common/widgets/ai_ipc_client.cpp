/*
 * This program source code file is part of Anvil.
 *
 * AI IPC Client — TCP client for Python AI backend communication.
 */

#include <widgets/ai_ipc_client.h>

#include <chrono>
#include <wx/log.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment( lib, "ws2_32.lib" )

    #define INVALID_SOCK INVALID_SOCKET
    #define CLOSE_SOCKET( s ) closesocket( s )

    static bool s_wsaInitialized = false;

    static void ensureWSA()
    {
        if( !s_wsaInitialized )
        {
            WSADATA wsaData;
            WSAStartup( MAKEWORD( 2, 2 ), &wsaData );
            s_wsaInitialized = true;
        }
    }
    using SOCKET_T = SOCKET;
#else
    #include <arpa/inet.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>

    #define INVALID_SOCK ( -1 )
    #define CLOSE_SOCKET( s ) close( s )

    static void ensureWSA() {}

    using SOCKET_T = int;
#endif


static const wxChar TraceAiIpc[] = wxT( "AI_IPC" );

/// How long openSocket() will wait for the loopback backend to accept.  The connect runs on
/// the UI thread, so this is also the worst-case stall a failed retry can cost — and three
/// frames (manager, schematic, board) each poll on their own timer, so it is paid up to
/// three times per round.  A loopback handshake is completed by the kernel the moment a
/// listener exists, independently of how busy the backend is, so this only has to cover
/// scheduling jitter; a closed port normally fails instantly and never reaches the deadline
/// at all.  It exists for ports that silently DROP the SYN (Windows' reserved/excluded port
/// ranges do this), where the OS would otherwise run its full ~2 s retry schedule.
static constexpr int kConnectTimeoutMs = 50;


/// Set/clear non-blocking mode on a socket, portably.
static bool setNonBlocking( SOCKET_T aSocket, bool aNonBlocking )
{
#ifdef _WIN32
    u_long mode = aNonBlocking ? 1 : 0;
    return ioctlsocket( aSocket, FIONBIO, &mode ) == 0;
#else
    int flags = fcntl( aSocket, F_GETFL, 0 );

    if( flags < 0 )
        return false;

    flags = aNonBlocking ? ( flags | O_NONBLOCK ) : ( flags & ~O_NONBLOCK );

    return fcntl( aSocket, F_SETFL, flags ) == 0;
#endif
}


/// connect() with a hard deadline.  Leaves the socket blocking again on success, so the
/// receive thread's recv() loop is unchanged.
static bool connectWithTimeout( SOCKET_T aSocket, const struct sockaddr_in& aAddr,
                                int aTimeoutMs )
{
    if( !setNonBlocking( aSocket, true ) )
    {
        // Can't arm the deadline; a blocking connect is still better than no connection.
        return connect( aSocket, (const struct sockaddr*) &aAddr, sizeof( aAddr ) ) == 0;
    }

    bool connected = false;

    if( connect( aSocket, (const struct sockaddr*) &aAddr, sizeof( aAddr ) ) == 0 )
    {
        connected = true;   // loopback usually completes immediately
    }
    else
    {
#ifdef _WIN32
        const bool inProgress = WSAGetLastError() == WSAEWOULDBLOCK;
#else
        const bool inProgress = errno == EINPROGRESS || errno == EWOULDBLOCK;
#endif

        if( inProgress )
        {
            fd_set writeSet;
            fd_set errorSet;
            FD_ZERO( &writeSet );
            FD_ZERO( &errorSet );
            FD_SET( aSocket, &writeSet );
            FD_SET( aSocket, &errorSet );

            struct timeval tv;
            tv.tv_sec  = aTimeoutMs / 1000;
            tv.tv_usec = ( aTimeoutMs % 1000 ) * 1000;

            const int nfds = static_cast<int>( aSocket ) + 1;   // ignored on Windows

            if( select( nfds, nullptr, &writeSet, &errorSet, &tv ) > 0
                && FD_ISSET( aSocket, &writeSet ) )
            {
                // Writable can still mean "connect failed" — ask for the real result.
                int err = 0;
#ifdef _WIN32
                int       len = sizeof( err );
#else
                socklen_t len = sizeof( err );
#endif

                if( getsockopt( aSocket, SOL_SOCKET, SO_ERROR,
                                reinterpret_cast<char*>( &err ), &len ) == 0 && err == 0 )
                {
                    connected = true;
                }
            }
        }
    }

    // Restore blocking mode either way: the caller closes the socket on failure, and the
    // receive thread expects a blocking recv().
    setNonBlocking( aSocket, false );

    return connected;
}


AI_IPC_CLIENT::AI_IPC_CLIENT( const std::string& aHost, int aPort ) :
        m_host( aHost ),
        m_port( aPort ),
        m_socket( INVALID_SOCK ),
        m_connected( false ),
        m_shutdown( false )
{
}


AI_IPC_CLIENT::~AI_IPC_CLIENT()
{
    Disconnect();
}


bool AI_IPC_CLIENT::openSocket()
{
    ensureWSA();

    m_socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

    if( m_socket == INVALID_SOCK )
    {
        wxLogTrace( TraceAiIpc, wxS( "Failed to create socket" ) );
        return false;
    }

    struct sockaddr_in serverAddr;
    memset( &serverAddr, 0, sizeof( serverAddr ) );
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons( static_cast<unsigned short>( m_port ) );
    inet_pton( AF_INET, m_host.c_str(), &serverAddr.sin_addr );

    // Connect with a short deadline instead of letting the OS run its full SYN-retry
    // schedule.  Connect() is called from the frames' retry timers, i.e. ON THE UI THREAD:
    // a stale ipc_port.txt pointing at a port that silently drops SYNs (rather than
    // refusing) froze the whole application for ~2 s on every single retry.  The backend
    // is always on loopback, so anything that has not accepted within this budget is not
    // there — try again on the next tick rather than blocking the UI.
    if( !connectWithTimeout( m_socket, serverAddr, kConnectTimeoutMs ) )
    {
        CLOSE_SOCKET( m_socket );
        m_socket = INVALID_SOCK;
        return false;
    }

    // Enable TCP keep-alive so the OS detects dead peers in seconds, not
    // hours. Without this, the receive thread can sit on recv() forever
    // even after the backend goes away — auto-reconnect would never fire.
    int yes = 1;
    setsockopt( m_socket, SOL_SOCKET, SO_KEEPALIVE,
                reinterpret_cast<const char*>( &yes ), sizeof( yes ) );

    m_connected.store( true );
    wxLogTrace( TraceAiIpc,
                wxString::Format( wxS( "Connected to AI backend at %s:%d" ), m_host, m_port ) );
    return true;
}


bool AI_IPC_CLIENT::Connect()
{
    if( m_connected.load() )
        return true;

    if( !openSocket() )
        return false;

    m_shutdown.store( false );
    m_receiveThread = std::thread( [this]() { receiveThread(); } );

    return true;
}


void AI_IPC_CLIENT::Disconnect()
{
    m_shutdown.store( true );
    m_connected.store( false );

    if( m_socket != INVALID_SOCK )
    {
        CLOSE_SOCKET( m_socket );
        m_socket = INVALID_SOCK;
    }

    if( m_receiveThread.joinable() )
        m_receiveThread.join();
}


void AI_IPC_CLIENT::SendRaw( const std::string& aJsonPayload )
{
    sendLengthPrefixed( aJsonPayload );
}


void AI_IPC_CLIENT::SendContextUpdate( const std::string& aContextJson )
{
    // Wrap in the expected protocol envelope
    std::string envelope = "{\"type\":\"context_update\",\"data\":" + aContextJson + "}";
    sendLengthPrefixed( envelope );
}


void AI_IPC_CLIENT::sendLengthPrefixed( const std::string& aPayload )
{
    if( !m_connected.load() )
        return;

    std::lock_guard<std::mutex> lock( m_sendMutex );

    try
    {
        uint32_t length = htonl( static_cast<uint32_t>( aPayload.size() ) );

        if( send( m_socket, reinterpret_cast<const char*>( &length ), 4, 0 ) != 4 )
        {
            wxLogTrace( TraceAiIpc, wxS( "Failed to send message length" ) );
            m_connected.store( false );
            return;
        }

        size_t totalSent = 0;

        while( totalSent < aPayload.size() )
        {
            int sent = send( m_socket, aPayload.c_str() + totalSent,
                             static_cast<int>( aPayload.size() - totalSent ), 0 );

            if( sent <= 0 )
            {
                wxLogTrace( TraceAiIpc, wxS( "Failed to send message payload" ) );
                m_connected.store( false );
                return;
            }

            totalSent += sent;
        }
    }
    catch( ... )
    {
        m_connected.store( false );
    }
}


void AI_IPC_CLIENT::receiveThread()
{
    wxLogTrace( TraceAiIpc, wxS( "Receive thread started" ) );

    // Outer loop: stay alive across reconnects until shutdown is requested.
    // Inside, the recv loop processes messages while m_connected is true;
    // when the socket drops, we close it, sleep, then openSocket() to reconnect.
    // Without this outer loop, a single dropped TCP connection killed the
    // thread permanently — that was the "first prompt refreshes, second
    // doesn't" symptom: the plugin handled the first revert, the socket
    // closed (peer side or RST), and the thread exited never to retry.
    while( !m_shutdown.load() )
    {
        // Inner loop: receive while connected.
        while( !m_shutdown.load() && m_connected.load() )
        {
            uint32_t netLength = 0;
            int      received = recv( m_socket, reinterpret_cast<char*>( &netLength ), 4, 0 );

            if( received != 4 )
            {
                if( !m_shutdown.load() )
                {
                    wxLogTrace( TraceAiIpc, wxS( "Connection lost (length read failed)" ) );
                    m_connected.store( false );
                }
                break;
            }

            uint32_t length = ntohl( netLength );

            if( length > 1024 * 1024 )
            {
                wxLogTrace( TraceAiIpc, wxString::Format( wxS( "Message too large: %u" ), length ) );
                m_connected.store( false );
                break;
            }

            std::string payload( length, '\0' );
            size_t      totalReceived = 0;
            bool        readOk = true;

            while( totalReceived < length )
            {
                received = recv( m_socket, &payload[totalReceived],
                                 static_cast<int>( length - totalReceived ), 0 );

                if( received <= 0 )
                {
                    if( !m_shutdown.load() )
                    {
                        wxLogTrace( TraceAiIpc, wxS( "Connection lost (payload read failed)" ) );
                        m_connected.store( false );
                    }
                    readOk = false;
                    break;
                }

                totalReceived += received;
            }

            if( !readOk )
                break;

            // Pass raw JSON strings to callback — let caller parse
            std::lock_guard<std::mutex> lock( m_mutex );

            if( m_commandCallback )
                m_commandCallback( "raw", payload );
        }

        if( m_shutdown.load() )
            break;

        // Connection dropped — close the dead socket cleanly and try
        // reconnecting after a brief wait. Polling interval is short so
        // a backend restart is recovered within a couple of seconds.
        if( m_socket != INVALID_SOCK )
        {
            CLOSE_SOCKET( m_socket );
            m_socket = INVALID_SOCK;
        }

        for( int i = 0; i < 20 && !m_shutdown.load(); ++i )
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

        if( m_shutdown.load() )
            break;

        wxLogTrace( TraceAiIpc, wxS( "Attempting reconnect..." ) );
        openSocket();
        // If openSocket() succeeded, m_connected is true and the inner
        // recv loop resumes. If it failed, the outer loop sleeps again
        // and retries — backend not yet up, no harm done.
    }

    wxLogTrace( TraceAiIpc, wxS( "Receive thread stopped" ) );
}
