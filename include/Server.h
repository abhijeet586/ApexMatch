/**
 * @file server.h
 * @brief tcp network layer for the apexmatch matching engine.
 * @author Abhijeet Senapati
 *
 * implements a lightweight tcp server that:
 *   - binds to a configurable port (default 8080).
 *   - accepts concurrent client connections.
 *   - dispatches each connection to a worker in the threadpool.
 *   - parses text-based trading commands and delegates to the orderbook.
 * cross-platform: supports both posix sockets and windows winsock2.
 * supported commands:
 *   submit buy  <price> <quantity>  — submit a buy limit order.
 *   submit sell <price> <quantity>  — submit a sell limit order.
 *   view                            — view top-of-book snapshot.
 *   quit                            — disconnect.
 */

#pragma once

#include "OrderBook.h"
#include "ThreadPool.h"

#include <string>
#include <atomic>
#include <cstdint>

//cross-platform socket abstraction

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

using namespace std;

class Server {
public:
    /**
     * @brief constructs the tcp server.
     * @param port           port to bind(e.g., 8080).
     * @param threadpoolsize number of worker threads for connection handling.
     * @param book           reference to the orderbook engine.
     * @throws runtime_error on winsock initialization failure(windows).
     */
    Server(uint16_t port, size_t threadPoolSize, OrderBook& book);

    /**
     * @brief destructor. stops the server and cleans up sockets.
     */
    ~Server();

    //non-copyable,non-movable.
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&)                 = delete;
    Server& operator=(Server&&)      = delete;

    /**
     * @brief starts the server: binds, listens, and enters the accept loop.
     * this call blocks until stop() is called from another thread or
     * a signal handler.
     * @throws runtime_error on socket creation, bind, or listen failure.
     */
    void start();

    /**
     * @brief gracefully stops the server.
     * closes the listening socket (which unblocks accept()), shuts down
     * the thread pool, and returns. safe to call from a signal handler
     * or another thread.
     */
    void stop();

private:
    /**
     * @brief main accept loop. dispatches new connections to the thread pool.
     */
    void acceptLoop();

    /**
     * @brief handles a single client connection (runs in a pool worker).
     * reads commands line-by-line, processes them, and sends responses.
     * connection is closed when the client disconnects or sends quit.
     * @param clientsocket the connected client socket descriptor.
     */
    void handleClient(SocketType clientSocket);

    /**
     * @brief parses and executes a single command string.
     * @param command the uppercased command from the client.
     * @return response string to send back to the client.
     */
    string processCommand(const string& command);

    /**
     * @brief cross-platform socket close.
     */
    static void closeSocket(SocketType sock);

    //members
    uint16_t        port_;          ///< listening port.
    SocketType      listenSocket_;  ///< server listening socket.
    OrderBook&      orderBook_;     ///< reference to the matching engine.
    ThreadPool      threadPool_;    ///< fixed-size connection handler pool.
    atomic<bool>    running_;       ///< server running flag.

#ifdef _WIN32
    bool wsaInitialized_;  ///< tracks wsastartup / wsacleanup state.
#endif
};

}//namespace apex
