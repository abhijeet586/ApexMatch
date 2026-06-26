/**
 * @file OrderBook.h
 * @brief Thread-safe Limit Order Book with Price-Time Priority matching.
 * @author Abhijeet Senapati
 *
 * Architecture:
 * ┌────────────┐       ┌──────────────┐       ┌───────────────────┐
 * │  Client    │──────>│  Incoming    │──────>│  Matching Engine  │
 * │  Threads   │ push  │  Queue       │ drain │  (Background)     │
 * │            │       │  (mutex +    │       │  ┌──────┐ ┌──────┐│
 * │  submit()  │       │   cond_var)  │       │  │ Bids │ │ Asks ││
 * └────────────┘       └──────────────┘       │  │(Max) │ │(Min) ││
 *                                              │  └──────┘ └──────┘│
 *                                              └───────────────────┘
 *
 * Concurrency Model:
 *   - submitOrder() is called from arbitrary client threads. It assigns
 *     an order ID, pushes to a mutex-protected incoming queue, and
 *     signals the matching engine via condition_variable.
 *   - The matching engine runs on a single dedicated thread, draining
 *     the incoming queue in batches and performing match attempts.
 *   - viewTopOfBook() acquires the book mutex for a consistent snapshot.
 *   - This design eliminates contention on the core order book structures
 *     from client threads — only the engine thread mutates bids_/asks_.
 */

#pragma once

#include "Order.h"

#include <set>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>
#include <string>

namespace apex {

class OrderBook {
public:
    /**
     * @brief Constructs the OrderBook and launches the matching engine thread.
     * @param symbol The ticker symbol this book manages (e.g., "AAPL").
     */
    explicit OrderBook(const std::string& symbol = "AAPL");

    /**
     * @brief Destructor. Signals shutdown and joins the matching engine thread.
     */
    ~OrderBook();

    // Non-copyable, non-movable.
    OrderBook(const OrderBook&)            = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&)                 = delete;
    OrderBook& operator=(OrderBook&&)      = delete;

    /**
     * @brief Submits a new limit order to the book.
     *
     * Thread-safe. Returns immediately after assigning an order ID and
     * enqueuing the order. The matching engine thread will process it
     * asynchronously.
     *
     * @param side     BUY or SELL.
     * @param price    Limit price (must be > 0).
     * @param quantity Number of shares/contracts (must be > 0).
     * @return The unique order ID assigned to the submitted order.
     */
    uint64_t submitOrder(Side side, double price, uint32_t quantity);

    /**
     * @brief Returns a formatted snapshot of the top of the order book.
     *
     * Thread-safe (acquires book mutex). Shows the best bid, best ask,
     * spread, book depth, and last executed trade.
     *
     * @return Multi-line string with the current book state.
     */
    std::string viewTopOfBook() const;

    /**
     * @brief Returns the ticker symbol managed by this book.
     */
    const std::string& symbol() const noexcept { return symbol_; }

    /**
     * @brief Gracefully shuts down the matching engine thread.
     * Safe to call multiple times (idempotent).
     */
    void shutdown();

private:
    // ─── Matching Engine ──────────────────────────────────────────────────

    /**
     * @brief Main loop for the dedicated matching engine thread.
     *
     * Waits on queueCV_ for incoming orders, drains the queue in batches,
     * inserts orders into the book, and invokes tryMatch().
     */
    void matchingEngineLoop();

    /**
     * @brief Attempts to match crossing orders on the bid and ask sides.
     *
     * Repeatedly checks if the best bid price >= best ask price.
     * On match, creates a Trade record, adjusts quantities, and removes
     * fully-filled orders from the book.
     *
     * Called only from the matching engine thread (no external locking needed
     * beyond the book mutex already held).
     */
    void tryMatch();

    // ─── Data ─────────────────────────────────────────────────────────────

    std::string symbol_;   ///< Ticker symbol (e.g., "AAPL").

    /**
     * Buy side — Max-Heap via std::set<Order, BidComparator>.
     * begin() always points to the best (highest-price, earliest-time) bid.
     */
    std::set<Order, BidComparator> bids_;

    /**
     * Sell side — Min-Heap via std::set<Order, AskComparator>.
     * begin() always points to the best (lowest-price, earliest-time) ask.
     */
    std::set<Order, AskComparator> asks_;

    /**
     * Incoming order queue — decouples client submission from matching.
     * Protected by queueMutex_ + queueCV_.
     */
    std::queue<Order> incomingQueue_;

    // ─── Synchronization ──────────────────────────────────────────────────

    mutable std::mutex      bookMutex_;    ///< Protects bids_, asks_ during reads.
    std::mutex              queueMutex_;   ///< Protects incomingQueue_.
    std::condition_variable queueCV_;      ///< Signals engine on new orders.

    // ─── Matching Engine Thread ───────────────────────────────────────────

    std::thread        matchingThread_;  ///< Dedicated matching engine thread.
    std::atomic<bool>  running_;         ///< Controls the engine loop lifecycle.

    // ─── ID Generators ────────────────────────────────────────────────────

    std::atomic<uint64_t> nextOrderId_{1};  ///< Monotonic order ID counter.
    std::atomic<uint64_t> nextTradeId_{1};  ///< Monotonic trade ID counter.

    // ─── Trade Log ────────────────────────────────────────────────────────

    std::vector<Trade>  tradeLog_;      ///< Chronological record of executed trades.
    mutable std::mutex  tradeMutex_;    ///< Protects tradeLog_.
};

} // namespace apex
