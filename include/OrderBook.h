/**
 * @file orderbook.h
 * @brief thread-safe limit order book with price-time priority matching.
 * @author Abhijeet Senapati
 *
 *                              architecture:
 * ┌────────────┐       ┌──────────────┐       ┌───────────────────┐
 * │  client    │──────>│  incoming    │──────>│  matching engine  │
 * │  threads   │ push  │  queue       │ drain │  (background)     │
 * │            │       │  (mutex +    │       │  ┌──────┐ ┌──────┐│
 * │  submit()  │       │   cond_var)  │       │  │ bids │ │ asks ││
 * └────────────┘       └──────────────┘       │  │(max) │ │(min) ││
*                                              │  └──────┘ └──────┘│
*                                              └───────────────────┘
 *
 * concurrency model:
 *   - submitorder() is called from arbitrary client threads. it assigns
 *     an order id, pushes to a mutex-protected incoming queue, and
 *     signals the matching engine via condition_variable.
 *   - the matching engine runs on a single dedicated thread, draining
 *     the incoming queue in batches and performing match attempts.
 *   - viewtopofbook() acquires the book mutex for a consistent snapshot.
 *   - this design eliminates contention on the core order book structures
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

using namespace std;

class OrderBook {
public:
    /**
     * @brief constructs the orderbook and launches the matching engine thread.
     * @param symbol the ticker symbol this book manages (e.g., "AAPL").
     */
    explicit OrderBook(const string& symbol = "AAPL");

    /**
     * @brief destructor.signals shutdown and joins the matching engine thread.
     */
    ~OrderBook();

    // non-copyable, non-movable.
    OrderBook(const OrderBook&)            = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&)                 = delete;
    OrderBook& operator=(OrderBook&&)      = delete;

    /**
     * @brief submits a new limit order to the book.
     * thread-safe. returns immediately after assigning an order id and
     * enqueuing the order. the matching engine thread will process it
     * asynchronously.
     * @param side     buy or sell.
     * @param price    limit price (must be > 0).
     * @param quantity number of shares/contracts (must be > 0).
     * @return the unique order id assigned to the submitted order.
     */
    uint64_t submitOrder(Side side, double price, uint32_t quantity);

    /**
     * @brief returns a formatted snapshot of the top of the order book.
     * thread-safe (acquires book mutex). shows the best bid, best ask,
     * spread, book depth, and last executed trade.
     * @return multi-line string with the current book state.
     */
    string viewTopOfBook() const;

    /**
     * @brief returns the ticker symbol managed by this book.
     */
    const string& symbol() const noexcept { return symbol_; }

    /**
     * @brief gracefully shuts down the matching engine thread.
     * safe to call multiple times (idempotent).
     */
    void shutdown();

private:
    //matching engine

    /**
     * @brief main loop for the dedicated matching engine thread.
     * waits on queuecv_ for incoming orders, drains the queue in batches,
     * inserts orders into the book, and invokes trymatch().
     */
    void matchingEngineLoop();

    /**
     * @brief attempts to match crossing orders on the bid and ask sides.
     * repeatedly checks if the best bid price >= best ask price.
     * on match, creates a trade record, adjusts quantities, and removes
     * fully-filled orders from the book.
     * called only from the matching engine thread (no external locking needed
     * beyond the book mutex already held).
     */
    void tryMatch();

    //data

    string symbol_;   ///< ticker symbol (e.g., "AAPL").

    /**
     * buy side — max-heap via set<order, bidcomparator>.
     * begin() always points to the best (highest-price, earliest-time) bid.
     */
    set<Order, BidComparator> bids_;

    /**
     * sell side — min-heap via set<order, askcomparator>.
     * begin() always points to the best (lowest-price, earliest-time) ask.
     */
    set<Order, AskComparator> asks_;

    /**
     * incoming order queue — decouples client submission from matching.
     * protected by queuemutex_ + queuecv_.
     */
    queue<Order> incomingQueue_;

    //synchronization
    mutable mutex      bookMutex_;    ///< protects bids_, asks_ during reads.
    mutex              queueMutex_;   ///< protects incomingqueue_.
    condition_variable queueCV_;      ///< signals engine on new orders.

    //matching engine thread
    thread        matchingThread_;  ///< dedicated matching engine thread.
    atomic<bool>  running_;         ///< controls the engine loop lifecycle.

    //id generators
    atomic<uint64_t> nextOrderId_{1};  ///< monotonic order id counter.
    atomic<uint64_t> nextTradeId_{1};  ///< monotonic trade id counter.

    //trade log
    vector<Trade>  tradeLog_;      ///< chronological record of executed trades.
    mutable mutex  tradeMutex_;    ///< protects tradelog_.
};

}//namespace apex
