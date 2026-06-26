/**
 * @file Server.h
 * @brief TCP network layer for the ApexMatch matching engine.
 * @author Abhijeet Senapati
 *
 * Implements a lightweight TCP server that:
 *   - Binds to a configurable port (default 8080).
 *   - Accepts concurrent client connections.
 *   - Dispatches each connection to a worker in the ThreadPool.
 *   - Parses text-based trading commands and delegates to the OrderBook.
 *
 * Cross-platform: supports both POSIX sockets and Windows Winsock2.
 *
 * Supported commands:
 *   SUBMIT BUY  <PRICE> <QUANTITY>  — Submit a buy limit order.
 *   SUBMIT SELL <PRICE> <QUANTITY>  — Submit a sell limit order.
 *   VIEW                            — View top-of-book snapshot.
 *   QUIT                            — Disconnect.
 */

#pragma once

#include "OrderBook.h"
#include "ThreadPool.h"

#include <string>
#include <atomic>
#include <cstdint>

// ═══════════════════════════════════════════════════════════════════════════════
//  Cross-Platform Socket Abstraction
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "ws2_32.lib")
    #endif
    using SocketType  = SOCKET;
    using SockLenType = int;
    constexpr SocketType INVALID_SOCK = INVALID_SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    using SocketType  = int;
    using SockLenType = socklen_t;
    constexpr SocketType INVALID_SOCK = -1;
#endif

namespace apex {

class Server {
public:
    /**
     * @brief Constructs the TCP server.
     * @param port           Port to bind (e.g., 8080).
     * @param threadPoolSize Number of worker threads for connection handling.
     * @param book           Reference to the OrderBook engine.
     * @throws std::runtime_error on Winsock initialization failure (Windows).
     */
    Server(uint16_t port, size_t threadPoolSize, OrderBook& book);

    /**
     * @brief Destructor. Stops the server and cleans up sockets.
     */
    ~Server();

    // Non-copyable, non-movable.
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&)                 = delete;
    Server& operator=(Server&&)      = delete;

    /**
     * @brief Starts the server: binds, listens, and enters the accept loop.
     *
     * This call blocks until stop() is called from another thread or
     * a signal handler.
     *
     * @throws std::runtime_error on socket creation, bind, or listen failure.
     */
    void start();

    /**
     * @brief Gracefully stops the server.
     *
     * Closes the listening socket (which unblocks accept()), shuts down
     * the thread pool, and returns. Safe to call from a signal handler
     * or another thread.
     */
    void stop();

private:
    /**
     * @brief Main accept loop. Dispatches new connections to the thread pool.
     */
    void acceptLoop();

    /**
     * @brief Handles a single client connection (runs in a pool worker).
     *
     * Reads commands line-by-line, processes them, and sends responses.
     * Connection is closed when the client disconnects or sends QUIT.
     *
     * @param clientSocket The connected client socket descriptor.
     */
    void handleClient(SocketType clientSocket);

    /**
     * @brief Parses and executes a single command string.
     * @param command The uppercased command from the client.
     * @return Response string to send back to the client.
     */
    std::string processCommand(const std::string& command);

    /**
     * @brief Cross-platform socket close.
     */
    static void closeSocket(SocketType sock);

    // ─── Members ──────────────────────────────────────────────────────────

    uint16_t        port_;          ///< Listening port.
    SocketType      listenSocket_;  ///< Server listening socket.
    OrderBook&      orderBook_;     ///< Reference to the matching engine.
    ThreadPool      threadPool_;    ///< Fixed-size connection handler pool.
    std::atomic<bool> running_;     ///< Server running flag.

#ifdef _WIN32
    bool wsaInitialized_;  ///< Tracks WSAStartup / WSACleanup state.
#endif
};

} // namespace apex
