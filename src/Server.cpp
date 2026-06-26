/**
 * @file Server.cpp
 * @brief Implementation of the TCP server for ApexMatch.
 * @author Abhijeet Senapati
 *
 * The server uses a classic accept-dispatch model:
 *   1. Main thread binds to port, enters accept loop.
 *   2. Each accepted connection is dispatched to the ThreadPool.
 *   3. A pool worker reads commands, calls processCommand(), writes responses.
 *   4. Graceful shutdown closes the listen socket to unblock accept().
 *
 * Telnet Compatibility Notes:
 *   - Input is buffered line-by-line (Windows Telnet sends char-at-a-time).
 *   - Telnet IAC negotiation sequences are stripped from input.
 *   - All output uses CRLF (\r\n) line endings as required by the Telnet spec.
 */

#include "Server.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <iomanip>

namespace apex {

// =====================================================================
//  Telnet Helpers
// =====================================================================

/**
 * @brief Converts LF (\n) to CRLF (\r\n) for Telnet-compatible output.
 *
 * The Telnet protocol (RFC 854) requires CRLF line endings. Without the
 * carriage return, the cursor moves down but does NOT return to column 0,
 * producing a "staircase" effect in Windows Telnet.
 */
static std::string toCRLF(const std::string& input) {
    std::string output;
    output.reserve(input.size() + input.size() / 10);  // Slight over-alloc.

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\n') {
            // Only add \r if it's not already \r\n.
            if (i == 0 || input[i - 1] != '\r') {
                output += '\r';
            }
            output += '\n';
        } else {
            output += input[i];
        }
    }
    return output;
}

/**
 * @brief Sends a string to a socket with automatic LF -> CRLF conversion.
 */
static void sendToClient(SocketType sock, const std::string& msg) {
    std::string crlf = toCRLF(msg);
    send(sock, crlf.c_str(), static_cast<int>(crlf.size()), 0);
}

/**
 * @brief Strips Telnet IAC (Interpret As Command) sequences from raw input.
 *
 * Windows Telnet sends negotiation bytes like 0xFF 0xFD 0x01 on connect.
 * These must be filtered out before treating the data as text input.
 * IAC sequences are 2 or 3 bytes starting with 0xFF.
 */
static std::string stripTelnetIAC(const char* data, int len) {
    std::string clean;
    clean.reserve(static_cast<size_t>(len));

    for (int i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(data[i]);

        if (c == 0xFF && i + 1 < len) {
            unsigned char next = static_cast<unsigned char>(data[i + 1]);
            if (next >= 0xFB && next <= 0xFE && i + 2 < len) {
                // 3-byte IAC: WILL/WONT/DO/DONT + option
                i += 2;
            } else if (next == 0xFF) {
                // Escaped 0xFF -> literal 0xFF
                clean += static_cast<char>(0xFF);
                i += 1;
            } else {
                // 2-byte IAC command
                i += 1;
            }
        } else {
            clean += static_cast<char>(c);
        }
    }
    return clean;
}

// =====================================================================
//  Construction / Destruction
// =====================================================================

Server::Server(uint16_t port, size_t threadPoolSize, OrderBook& book)
    : port_(port)
    , listenSocket_(INVALID_SOCK)
    , orderBook_(book)
    , threadPool_(threadPoolSize)
    , running_(false)
#ifdef _WIN32
    , wsaInitialized_(false)
#endif
{
#ifdef _WIN32
    // Initialize Windows Sockets API.
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        throw std::runtime_error(
            "[Server] WSAStartup failed with error: "
            + std::to_string(result));
    }
    wsaInitialized_ = true;
    std::cout << "[Server] Winsock initialized.\n";
#endif
}

Server::~Server() {
    stop();
#ifdef _WIN32
    if (wsaInitialized_) {
        WSACleanup();
        std::cout << "[Server] Winsock cleaned up.\n";
    }
#endif
}

// =====================================================================
//  Start / Stop
// =====================================================================

void Server::start() {
    // -- Create socket --
    listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket_ == INVALID_SOCK) {
        throw std::runtime_error("[Server] Failed to create TCP socket.");
    }

    // -- Set SO_REUSEADDR to avoid "address already in use" on restart --
    int opt = 1;
    setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    // -- Bind --
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (bind(listenSocket_,
             reinterpret_cast<sockaddr*>(&addr),
             sizeof(addr)) < 0) {
        closeSocket(listenSocket_);
        throw std::runtime_error(
            "[Server] Failed to bind to port " + std::to_string(port_)
            + ". Is the port already in use?");
    }

    // -- Listen --
    if (listen(listenSocket_, SOMAXCONN) < 0) {
        closeSocket(listenSocket_);
        throw std::runtime_error("[Server] Failed to listen on socket.");
    }

    running_ = true;

    std::cout << "\n"
        << "  +------------------------------------------------+\n"
        << "  |          ApexMatch Trading Engine               |\n"
        << "  |                                                |\n"
        << "  |   Symbol:  " << orderBook_.symbol();
    for (size_t i = orderBook_.symbol().size(); i < 36; ++i) std::cout << ' ';
    std::cout << "|\n"
        << "  |   Port:    " << port_;
    {
        std::string portStr = std::to_string(port_);
        for (size_t i = portStr.size(); i < 36; ++i) std::cout << ' ';
    }
    std::cout << "|\n"
        << "  |   Workers: " << threadPool_.size();
    {
        std::string wsStr = std::to_string(threadPool_.size());
        for (size_t i = wsStr.size(); i < 36; ++i) std::cout << ' ';
    }
    std::cout << "|\n"
        << "  |   Status:  LIVE - Accepting connections        |\n"
        << "  +------------------------------------------------+\n"
        << "\n"
        << "  Connect with:  telnet localhost " << port_ << "\n"
        << "            or:  ncat localhost " << port_ << "\n\n";

    // Enter the accept loop (blocks until stop() is called).
    acceptLoop();
}

void Server::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;  // Already stopped.
    }

    // Close the listening socket to break the accept() call.
    closeSocket(listenSocket_);
    listenSocket_ = INVALID_SOCK;

    threadPool_.shutdown();
    std::cout << "[Server] Stopped.\n";
}

// =====================================================================
//  Accept Loop (main thread)
// =====================================================================

void Server::acceptLoop() {
    while (running_.load(std::memory_order_acquire)) {
        sockaddr_in clientAddr{};
        SockLenType addrLen = sizeof(clientAddr);

        SocketType clientSocket = accept(
            listenSocket_,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &addrLen
        );

        if (clientSocket == INVALID_SOCK) {
            if (running_.load(std::memory_order_acquire)) {
                std::cerr << "[Server] accept() failed. Continuing...\n";
            }
            continue;  // On shutdown, listenSocket_ is closed -> accept fails.
        }

        // Log the incoming connection.
        char clientIP[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
        std::cout << "[Server] New connection from "
                  << clientIP << ":" << ntohs(clientAddr.sin_port) << "\n";

        // Dispatch to the thread pool -- no new thread per connection.
        threadPool_.enqueue([this, clientSocket]() {
            handleClient(clientSocket);
        });
    }
}

// =====================================================================
//  Client Handler (runs in a thread pool worker)
// =====================================================================

void Server::handleClient(SocketType clientSocket) {
    // -- Send welcome banner --
    sendToClient(clientSocket,
        "\n"
        "  +======================================+\n"
        "  |    Welcome to ApexMatch v1.0.0       |\n"
        "  |    Symbol: " + orderBook_.symbol() + "                       |\n"
        "  +======================================+\n"
        "  |  Commands:                           |\n"
        "  |    SUBMIT BUY  <PRICE> <QTY>         |\n"
        "  |    SUBMIT SELL <PRICE> <QTY>         |\n"
        "  |    VIEW                              |\n"
        "  |    QUIT                              |\n"
        "  +======================================+\n"
        "\n"
        "apex> "
    );

    // -- Line-buffered command read loop --
    //
    // CRITICAL: Windows Telnet sends each keystroke as a separate TCP
    // segment (character-at-a-time mode). We must buffer incoming bytes
    // and only process a command when we see a newline ('\n' or '\r').
    //
    char buffer[1024];
    std::string lineBuffer;  // Accumulates bytes until a newline arrives.

    while (running_.load(std::memory_order_acquire)) {
        std::memset(buffer, 0, sizeof(buffer));
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (bytesRead <= 0) {
            // Client disconnected or error.
            break;
        }

        // Strip any telnet IAC negotiation sequences.
        std::string chunk = stripTelnetIAC(buffer, bytesRead);

        // Append to the line buffer.
        lineBuffer += chunk;

        // Normalize: replace \r\n and standalone \r with \n.
        // Telnet may send \r\n, \r\0, or just \r depending on the client.
        {
            std::string normalized;
            normalized.reserve(lineBuffer.size());
            for (size_t i = 0; i < lineBuffer.size(); ++i) {
                if (lineBuffer[i] == '\r') {
                    normalized += '\n';
                    // Skip a following \n or \0 if present.
                    if (i + 1 < lineBuffer.size() &&
                        (lineBuffer[i + 1] == '\n' || lineBuffer[i + 1] == '\0')) {
                        ++i;
                    }
                } else if (lineBuffer[i] == '\0') {
                    // Skip stray NUL bytes.
                } else {
                    normalized += lineBuffer[i];
                }
            }
            lineBuffer = std::move(normalized);
        }

        // Process all complete lines in the buffer.
        size_t pos;
        while ((pos = lineBuffer.find('\n')) != std::string::npos) {
            // Extract the line (everything before the newline).
            std::string command = lineBuffer.substr(0, pos);
            lineBuffer.erase(0, pos + 1);

            // Strip all non-printable / control characters.
            command.erase(
                std::remove_if(command.begin(), command.end(),
                    [](unsigned char c) { return c < 0x20 || c > 0x7E; }),
                command.end());

            // Skip empty lines.
            if (command.empty()) {
                continue;
            }

            // Uppercase for case-insensitive matching.
            std::string upper = command;
            std::transform(upper.begin(), upper.end(), upper.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::toupper(c));
                           });

            // -- QUIT / EXIT --
            if (upper == "QUIT" || upper == "EXIT") {
                sendToClient(clientSocket, "Goodbye! Connection closed.\n");
                closeSocket(clientSocket);
                return;  // Exit the handler entirely.
            }

            // -- Process command --
            std::string response = processCommand(upper);
            response += "apex> ";
            sendToClient(clientSocket, response);
        }

        // If lineBuffer is getting too large without a newline, flush it.
        // This prevents a malicious client from consuming unbounded memory.
        if (lineBuffer.size() > 4096) {
            sendToClient(clientSocket,
                "ERROR: Input too long. Disconnecting.\n");
            break;
        }
    }

    closeSocket(clientSocket);
}

// =====================================================================
//  Command Processor
// =====================================================================

std::string Server::processCommand(const std::string& command) {
    std::istringstream iss(command);
    std::string token;
    iss >> token;

    // -- SUBMIT BUY|SELL <PRICE> <QUANTITY> --
    if (token == "SUBMIT") {
        std::string sideStr;
        double price      = 0.0;
        uint32_t quantity = 0;

        if (!(iss >> sideStr >> price >> quantity)) {
            return "ERROR: Usage: SUBMIT BUY|SELL <PRICE> <QUANTITY>\n";
        }

        Side side;
        if (sideStr == "BUY") {
            side = Side::BUY;
        } else if (sideStr == "SELL") {
            side = Side::SELL;
        } else {
            return "ERROR: Side must be BUY or SELL.\n";
        }

        if (price <= 0.0) {
            return "ERROR: Price must be a positive number.\n";
        }
        if (quantity == 0) {
            return "ERROR: Quantity must be greater than zero.\n";
        }

        uint64_t orderId = orderBook_.submitOrder(side, price, quantity);

        std::ostringstream oss;
        oss << "ACK: Order #" << orderId << " accepted -- "
            << sideStr << " " << quantity << " @ "
            << std::fixed << std::setprecision(2) << price << "\n";
        return oss.str();
    }

    // -- VIEW --
    if (token == "VIEW") {
        return orderBook_.viewTopOfBook();
    }

    // -- HELP --
    if (token == "HELP") {
        return
            "\nAvailable commands:\n"
            "  SUBMIT BUY  <PRICE> <QTY>  -- Place a buy limit order\n"
            "  SUBMIT SELL <PRICE> <QTY>  -- Place a sell limit order\n"
            "  VIEW                       -- Show top-of-book snapshot\n"
            "  HELP                       -- Show this help message\n"
            "  QUIT                       -- Disconnect\n\n";
    }

    return "ERROR: Unknown command '" + token
         + "'. Type HELP for available commands.\n";
}

// =====================================================================
//  Utility
// =====================================================================

void Server::closeSocket(SocketType sock) {
    if (sock == INVALID_SOCK) return;
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

} // namespace apex
