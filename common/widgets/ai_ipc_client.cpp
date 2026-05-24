/*
 * This program source code file is part of Envil.
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
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>

    #define INVALID_SOCK ( -1 )
    #define CLOSE_SOCKET( s ) close( s )

    static void ensureWSA() {}
#endif


static const wxChar TraceAiIpc[] = wxT( "AI_IPC" );


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

    if( connect( m_socket, (struct sockaddr*) &serverAddr, sizeof( serverAddr ) ) != 0 )
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
