/**
 * @file server.cpp
 * @brief implementation of the tcp server for apexmatch.
 * @author Abhijeet Senapati
 *
 * the server uses a classic accept-dispatch model:
 *   1. main thread binds to port, enters accept loop.
 *   2. each accepted connection is dispatched to the threadpool.
 *   3. a pool worker reads commands, calls processcommand(), writes responses.
 *   4. graceful shutdown closes the listen socket to unblock accept().
 *
 * telnet compatibility notes:
 *   - input is buffered line-by-line (windows telnet sends char-at-a-time).
 *   - telnet iac negotiation sequences are stripped from input.
 *   - all output uses crlf (\r\n) line endings as required by the telnet spec.
 */

#include "Server.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <iomanip>

using namespace std;

namespace apex {
//telnet helpers
/**
 * @brief converts lf (\n) to crlf (\r\n) for telnet-compatible output.
 * the telnet protocol (rfc 854) requires crlf line endings.Without the
 * carriage return, the cursor moves down but does not return to column 0,
 * producing a "staircase" effect in windows telnet.
 */
static string toCRLF(const string& input) {
    string output;
    output.reserve(input.size() + input.size() / 10);//slight over-alloc.

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\n') {
            //only add \r if it's not already \r\n.
            if (i==0||input[i - 1]!='\r') {
                output+='\r';
            }
            output+='\n';
        } else {
            output+=input[i];
        }
    }
    return output;
}

/**
 * @brief sends a string to a socket with automatic lf -> crlf conversion.
 */
static void sendToClient(SocketType sock, const string& msg) {
    string crlf = toCRLF(msg);
    send(sock, crlf.c_str(), static_cast<int>(crlf.size()), 0);
}

/**
 * @brief strips telnet iac (interpret as command) sequences from raw input.
 * windows telnet sends negotiation bytes like 0xff 0xfd 0x01 on connect.
 * these must be filtered out before treating the data as text input.
 * iac sequences are 2 or 3 bytes starting with 0xff.
 */
static string stripTelnetIAC(const char* data, int len) {
    string clean;
    clean.reserve(static_cast<size_t>(len));

    for (int i=0; i<len;++i) {
        unsigned char c=static_cast<unsigned char>(data[i]);

        if (c==0xFF&&i+1<len) {
            unsigned char next = static_cast<unsigned char>(data[i + 1]);
            if (next>=0xFB&&next<=0xFE&&i+2<len) {
                //3-byte iac: will/wont/do/dont + option
                i += 2;
            } else if (next==0xFF) {
                //escaped 0xff -> literal 0xff
                clean+=static_cast<char>(0xFF);
                i+=1;
            } else {
                //2-byte iac command
                i+=1;
            }
        } else {
            clean+=static_cast<char>(c);
        }
    }
    return clean;
}

//construction/destruction

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
    //initialize windows sockets api.
    WSADATA wsaData;
    int result=WSAStartup(MAKEWORD(2,2),&wsaData);
    if(result!=0) {
        throw runtime_error(
            "[Server] WSAStartup failed with error: "+to_string(result));
    }
    wsaInitialized_=true;
    cout<<"[Server] Winsock initialized.\n";
#endif
}
Server::~Server() {
    stop();
#ifdef _WIN32
    if (wsaInitialized_) {
        WSACleanup();
        cout << "[Server] Winsock cleaned up.\n";
    }
#endif
}
void Server::start() {
    //creating socket
    listenSocket_ = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (listenSocket_ == INVALID_SOCK) {
        throw runtime_error("[Server] Failed to create TCP socket.");
    }
    //set so_reuseaddr to avoid "address already in use" on restart
    int opt = 1;
    setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
    //bind
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (::bind(listenSocket_,
             reinterpret_cast<sockaddr*>(&addr),
             sizeof(addr)) < 0) {
        closeSocket(listenSocket_);
        throw runtime_error(
            "[Server] Failed to bind to port " + to_string(port_)+".Is the port already in use?");
    }
    //listen
    if (listen(listenSocket_, SOMAXCONN) < 0) {
        closeSocket(listenSocket_);
        throw runtime_error("[Server] Failed to listen on socket.");
    }
    running_ = true;
    cout << "\n"
        << "  +------------------------------------------------+\n"
        << "  |          ApexMatch Trading Engine               |\n"
        << "  |                                                |\n"
        << "  |   Symbol:  " << orderBook_.symbol();
    for (size_t i = orderBook_.symbol().size(); i < 36; ++i) cout << ' ';
    cout << "|\n"
        << "  |   Port:    " << port_;
    {
        string portStr = to_string(port_);
        for (size_t i = portStr.size(); i < 36; ++i) cout << ' ';
    }
    cout << "|\n"
        << "  |   Workers: " << threadPool_.size();
    {
        string wsStr = to_string(threadPool_.size());
        for (size_t i = wsStr.size(); i < 36; ++i) cout << ' ';
    }
    cout << "|\n"
        << "  |   Status:  LIVE - Accepting connections        |\n"
        << "  +------------------------------------------------+\n"
        << "\n"
        << "  Connect with:  telnet localhost " << port_ << "\n"
        << "            or:  ncat localhost " << port_ << "\n\n";

    //enter the accept loop (blocks until stop() is called).
    acceptLoop();
}

void Server::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;//already stopped.
    }

    //close the listening socket to break the accept() call.
    closeSocket(listenSocket_);
    listenSocket_ = INVALID_SOCK;

    threadPool_.shutdown();
    cout << "[Server] Stopped.\n";
}
//accept loop (main thread)
void Server::acceptLoop() {
    while (running_.load(memory_order_acquire)) {
        sockaddr_in clientAddr{};
        SockLenType addrLen = sizeof(clientAddr);
        SocketType clientSocket = accept(
            listenSocket_,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &addrLen
        );
        if (clientSocket == INVALID_SOCK) {
            if (running_.load(memory_order_acquire)) {
                cerr << "[Server] accept() failed. Continuing...\n";
            }
            continue;  //on shutdown,listensocket_ is closed->accept fails.
        }
        //log the incoming connection
        char clientIP[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
        cout<<"[Server] New connection from "<<clientIP<<":"<< ntohs(clientAddr.sin_port)<<"\n";

        //dispatch to the thread pool - no new thread per connection
        threadPool_.enqueue([this, clientSocket]() {
            handleClient(clientSocket);
        });
    }
}
//client handler (runs in a thread pool worker)
void Server::handleClient(SocketType clientSocket) {
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
    char buffer[1024];
    string lineBuffer;//accumulates bytes until a newline arrives
    while (running_.load(memory_order_acquire)) {
        memset(buffer, 0, sizeof(buffer));
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (bytesRead <= 0) {
            //client disconnected or error
            break;
        }

        //strip any telnet iac negotiation sequences
        string chunk=stripTelnetIAC(buffer,bytesRead);

        //append to the line buffer
        lineBuffer+=chunk;
        {
            string normalized;
            normalized.reserve(lineBuffer.size());
            for (size_t i = 0; i < lineBuffer.size(); ++i) {
                if (lineBuffer[i] == '\r') {
                    normalized += '\n';
                    //skip a following \n or \0 if present
                    if (i + 1 < lineBuffer.size() &&
                        (lineBuffer[i + 1] == '\n' || lineBuffer[i + 1] == '\0')) {
                        ++i;
                    }
                } else if (lineBuffer[i] == '\0') {
                    //skip stray nul bytes
                } else {
                    normalized += lineBuffer[i];
                }
            }
            lineBuffer = move(normalized);
        }

        //process all complete lines in the buffer
        size_t pos;
        while ((pos = lineBuffer.find('\n')) != string::npos) {
            //extract the line(everything before the newline)
            string command = lineBuffer.substr(0, pos);
            lineBuffer.erase(0, pos + 1);
            //strip all non-printable / control characters
            command.erase(
                remove_if(command.begin(), command.end(),
                    [](unsigned char c) { return c < 0x20 || c > 0x7E; }),
                command.end());
            //skip empty lines
            if (command.empty()) {
                continue;
            }
            //uppercase for case-insensitive matching
            string upper = command;
            transform(upper.begin(), upper.end(), upper.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(toupper(c));
                           });

            //exit
            if (upper == "QUIT" || upper == "EXIT") {
                sendToClient(clientSocket, "Goodbye! Connection closed.\n");
                closeSocket(clientSocket);
                return;  //exit the handler entirely.
            }

            //process command
            string response = processCommand(upper);
            response += "apex> ";
            sendToClient(clientSocket, response);
        }

        //if linebuffer is getting too large without a newline, flush it
        //this prevents a malicious client from consuming unbounded memory
        if (lineBuffer.size() > 4096) {
            sendToClient(clientSocket,
                "ERROR: Input too long. Disconnecting.\n");
            break;
        }
    }
    closeSocket(clientSocket);
}
//  command processor
string Server::processCommand(const string& command) {
    istringstream iss(command);
    string token;
    iss >> token;

    //submit buy|sell <price> <quantity>
    if (token == "SUBMIT") {
        string sideStr;
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

        ostringstream oss;
        oss << "ACK: Order #" << orderId << " accepted -- "
            << sideStr << " " << quantity << " @ "
            << fixed << setprecision(2) << price << "\n";
        return oss.str();
    }

    //view
    if (token == "VIEW") {
        return orderBook_.viewTopOfBook();
    }
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

//utility
void Server::closeSocket(SocketType sock){
    if (sock == INVALID_SOCK) return;
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}
}//namespace apex
