/**
 * @file main.cpp
 * @brief Entry point for the ApexMatch Order Matching Engine.
 * @author Abhijeet Senapati
 *
 * Initializes the OrderBook and TCP Server, registers signal handlers
 * for graceful shutdown (SIGINT / SIGTERM), and enters the server's
 * blocking accept loop.
 */

#include "Server.h"
#include "OrderBook.h"

#include <iostream>
#include <csignal>
#include <memory>
#include <cstdlib>

// ═══════════════════════════════════════════════════════════════════════════════
//  Global server pointer for signal-handler access
// ═══════════════════════════════════════════════════════════════════════════════

static std::unique_ptr<apex::Server> g_server;

/**
 * @brief Signal handler for graceful shutdown.
 *
 * Invoked on SIGINT (Ctrl+C) or SIGTERM. Calls Server::stop() which
 * closes the listening socket and unblocks the accept loop.
 */
void signalHandler(int signum) {
    std::cout << "\n[Main] Caught signal " << signum
              << ". Initiating graceful shutdown...\n";
    if (g_server) {
        g_server->stop();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Main
// ═══════════════════════════════════════════════════════════════════════════════

int main() {
    // ── Configuration ──
    constexpr uint16_t    PORT             = 8080;
    constexpr size_t      THREAD_POOL_SIZE = 4;
    constexpr const char* SYMBOL           = "AAPL";

    // -- ASCII Art Banner --
    std::cout << R"(

       _____                  __  ___      __       __
      /  _  \ ______  ___ ___/  |/  /___ _/  |_ ___\ \___
     /  /_\  \\____ \/  _/ __ \   __\   \/  _  \\   __/  _\
    /    |    \  |_> >  \_\  \/|  |  |   |  (_)  \|  | \  \_
    \____|____/   __/ \___/____/|__|  |___|\_____/ |__|  \___\
              |__|
            ___  ___       _       _
           |   \/   | __ _| |_ ___| |__
           | |\  /| |/ _` | __/ __| '_ \
           | | \/ | | (_| | || (__| | | |
           |_|    |_|\__,_|\__\___|_| |_|

    )" << "\n";

    std::cout << "      High-Concurrency Order Matching Engine\n";
    std::cout << "      Author: Abhijeet Senapati\n";
    std::cout << "      C++17 | Price-Time Priority | Thread Pool\n\n";

    // ── Register signal handlers for graceful shutdown ──
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        // ── Initialize the Order Book ──
        apex::OrderBook orderBook(SYMBOL);

        // ── Initialize and start the TCP Server ──
        g_server = std::make_unique<apex::Server>(
            PORT, THREAD_POOL_SIZE, orderBook);

        std::cout << "[Main] Starting ApexMatch on port "
                  << PORT << " with " << THREAD_POOL_SIZE
                  << " worker threads...\n\n";

        // This call blocks until the server is stopped.
        g_server->start();

        // ── Cleanup ──
        g_server.reset();
        orderBook.shutdown();

    } catch (const std::exception& e) {
        std::cerr << "\n[FATAL] " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "[Main] ApexMatch terminated cleanly.\n";
    return EXIT_SUCCESS;
}
