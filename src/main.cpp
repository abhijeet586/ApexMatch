/**
 * @file main.cpp
 * @brief entry point for the apexmatch order matching engine.
 * @author Abhijeet Senapati
 *
 * initializes the orderbook and tcp server,registers signal handlers
 * for graceful shutdown (sigint/sigterm),and enters the server's
 * blocking accept loop.
 */

#include "Server.h"
#include "OrderBook.h"

#include <iostream>
#include <csignal>
#include <memory>
#include <cstdlib>

using namespace std;

//global server pointer for signal-handler access
static unique_ptr<apex::Server> g_server;
/**
 * @brief signal handler for graceful shutdown.
 *
 * invoked on sigint (ctrl+c) or sigterm. calls server::stop() which
 * closes the listening socket and unblocks the accept loop.
 */
void signalHandler(int signum) {
    cout << "\n[Main] Caught signal " << signum
              << ". Initiating graceful shutdown...\n";
    if (g_server) {
        g_server->stop();
    }
}

int main() {
    //config
    constexpr uint16_t    PORT             = 8080;
    constexpr size_t      THREAD_POOL_SIZE = 4;
    constexpr const char* SYMBOL           = "AAPL";
    cout << R"(

       _____                  __  ___      __       __
      /  _  \ ______  ___ ___/  |/  /___ _/  |_ ___\ \___
     /  /_\  \\____ \/  _/ __ \   __\   \/  _  \\   __/  _\
    /    |    \  |_> >  \_\  \/|  |  |   |  (_)  \|  | \  \_
    \____|____/   __/ \___/____/|__|  |___|\_____ / |__|  \___\
              |__|
            ___  ___       _       _
           |   \/   | __ _| |_ ___| |__
           | |\  /| |/ _` | __/ __| '_ \
           | | \/ | | (_| | || (__| | | |
           |_|    |_|\__,_|\__\___|_| |_|

    )" << "\n";

    cout << "      High-Concurrency Order Matching Engine\n";
    cout << "      Author: Abhijeet Senapati\n";
    cout << "      C++17 | Price-Time Priority | Thread Pool\n\n";

    //register signal handlers for shutdown
    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    try {
        //initialize the order book
        apex::OrderBook orderBook(SYMBOL);

        //initialize and start the tcp server
        g_server = make_unique<apex::Server>(
            PORT, THREAD_POOL_SIZE, orderBook);

        cout << "[Main] Starting ApexMatch on port "
                  << PORT << " with " << THREAD_POOL_SIZE
                  << " worker threads...\n\n";

        //this call blocks until the server is stopped.
        g_server->start();

        g_server.reset();
        orderBook.shutdown();

    } catch (const exception& e) {
        cerr << "\n[FATAL] " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    cout << "[Main] ApexMatch terminated cleanly.\n";
    return EXIT_SUCCESS;
}
