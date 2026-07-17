/*
 * This program source code file is part of Anvil.
 *
 * AI IPC Client — sends design context to the Python AI backend
 * and receives commands (place component, highlight error, etc.)
 *
 * Protocol: Length-prefixed JSON over TCP (127.0.0.1:5555)
 *   [4 bytes big-endian length][JSON payload]
 */

#ifndef ANVIL_AI_IPC_CLIENT_H
#define ANVIL_AI_IPC_CLIENT_H

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>


/**
 * Bidirectional TCP client for communicating with the Anvil AI backend.
 *
 * Runs a background thread for receiving commands from Python.
 * Provides thread-safe methods for sending context updates from the main thread.
 *
 * Public API uses std::string (JSON serialized) to avoid nlohmann/json linkage issues.
 */
class AI_IPC_CLIENT
{
public:
    AI_IPC_CLIENT( const std::string& aHost = "127.0.0.1", int aPort = 5555 );
    ~AI_IPC_CLIENT();

    bool Connect();
    void Disconnect();
    bool IsConnected() const { return m_connected.load(); }

    /**
     * Update the TCP port. No-op if already connected; takes effect on next Connect().
     */
    void SetPort( int aPort ) { m_port = aPort; }
    int  GetPort() const { return m_port; }

    /**
     * Send a raw JSON string to the Python backend.
     * Thread-safe.
     */
    void SendRaw( const std::string& aJsonPayload );

    /**
     * Send a context update JSON string.
     */
    void SendContextUpdate( const std::string& aContextJson );

    /**
     * Register a callback for commands received from the Python backend.
     * Callback receives (action_string, data_json_string).
     */
    void SetCommandCallback( std::function<void( const std::string&, const std::string& )> aCallback )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_commandCallback = aCallback;
    }

private:
    void sendLengthPrefixed( const std::string& aPayload );
    void receiveThread();

    /// Open the TCP socket + enable TCP keep-alive. Sets m_connected on
    /// success. Does NOT start the receive thread (caller handles that).
    /// Returns true on connect, false on failure (caller can retry later).
    bool openSocket();

    std::string m_host;
    int         m_port;

#ifdef _WIN32
    uintptr_t   m_socket;
#else
    int         m_socket;
#endif

    std::atomic<bool> m_connected;
    std::atomic<bool> m_shutdown;

    std::thread m_receiveThread;
    std::mutex  m_mutex;
    std::mutex  m_sendMutex;

    std::function<void( const std::string&, const std::string& )> m_commandCallback;
};

#endif // ANVIL_AI_IPC_CLIENT_H
