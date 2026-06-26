/**
 * @file OrderBook.cpp
 * @brief Implementation of the thread-safe Limit Order Book.
 * @author Abhijeet Senapati
 *
 * Key implementation details:
 *   - The matching engine runs on a dedicated background thread, consuming
 *     incoming orders in batches to minimize lock contention.
 *   - tryMatch() implements aggressive matching: it loops until the best
 *     bid no longer crosses the best ask.
 *   - Partial fills are supported: when two orders cross but have different
 *     quantities, the smaller side is fully filled and the larger side's
 *     remaining quantity is re-inserted into the book.
 *   - Trade execution price follows the resting order (the one that arrived
 *     earlier), which is the standard exchange convention.
 */

#include "OrderBook.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace apex {

// ═══════════════════════════════════════════════════════════════════════════════
//  Construction / Destruction
// ═══════════════════════════════════════════════════════════════════════════════

OrderBook::OrderBook(const std::string& symbol)
    : symbol_(symbol)
    , running_(true)
{
    // Launch the dedicated matching engine thread.
    matchingThread_ = std::thread(&OrderBook::matchingEngineLoop, this);
    std::cout << "[OrderBook] Matching engine started for "
              << symbol_ << ".\n";
}

OrderBook::~OrderBook() {
    shutdown();
}

void OrderBook::shutdown() {
    // Atomically flip running_ from true to false exactly once.
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;  // Already shut down — idempotent.
    }

    queueCV_.notify_all();  // Wake the engine thread so it can exit.

    if (matchingThread_.joinable()) {
        matchingThread_.join();
    }

    std::cout << "[OrderBook] Matching engine stopped for "
              << symbol_ << ".\n";
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Order Submission (called from client threads)
// ═══════════════════════════════════════════════════════════════════════════════

uint64_t OrderBook::submitOrder(Side side, double price, uint32_t quantity) {
    Order order;
    order.id        = nextOrderId_.fetch_add(1, std::memory_order_relaxed);
    order.side      = side;
    order.price     = price;
    order.quantity   = quantity;
    order.timestamp = std::chrono::steady_clock::now();

    // Enqueue the order for the matching engine.
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        incomingQueue_.push(order);
    }

    // Signal the matching engine that work is available.
    queueCV_.notify_one();

    std::cout << "[OrderBook] Queued " << order.toString() << "\n";
    return order.id;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  View (called from client threads)
// ═══════════════════════════════════════════════════════════════════════════════

std::string OrderBook::viewTopOfBook() const {
    std::lock_guard<std::mutex> lock(bookMutex_);

    std::ostringstream oss;
    oss << "\n";
    oss << "  +=======================================+\n";
    oss << "  |       Order Book: " << symbol_;
    // Pad to align the right border.
    for (size_t i = symbol_.size(); i < 20; ++i) oss << ' ';
    oss << "|\n";
    oss << "  +=======================================+\n";

    // -- Best Bid --
    if (!bids_.empty()) {
        const auto& best = *bids_.begin();
        oss << "  |  Best Bid:  "
            << std::fixed << std::setprecision(2) << std::setw(10) << best.price
            << " x " << std::setw(6) << best.quantity
            << "     |\n";
    } else {
        oss << "  |  Best Bid:        ---                |\n";
    }

    // -- Best Ask --
    if (!asks_.empty()) {
        const auto& best = *asks_.begin();
        oss << "  |  Best Ask:  "
            << std::fixed << std::setprecision(2) << std::setw(10) << best.price
            << " x " << std::setw(6) << best.quantity
            << "     |\n";
    } else {
        oss << "  |  Best Ask:        ---                |\n";
    }

    // -- Spread --
    oss << "  +---------------------------------------+\n";
    if (!bids_.empty() && !asks_.empty()) {
        double spread = asks_.begin()->price - bids_.begin()->price;
        oss << "  |  Spread:    "
            << std::fixed << std::setprecision(2) << std::setw(10) << spread
            << "              |\n";
    } else {
        oss << "  |  Spread:          N/A                |\n";
    }

    oss << "  |  Bid Depth: " << std::setw(10) << bids_.size()
        << " orders       |\n";
    oss << "  |  Ask Depth: " << std::setw(10) << asks_.size()
        << " orders       |\n";

    // -- Last Trade --
    oss << "  +---------------------------------------+\n";
    {
        std::lock_guard<std::mutex> tLock(tradeMutex_);
        if (!tradeLog_.empty()) {
            const auto& last = tradeLog_.back();

            // Build the inner content, then pad to fixed width.
            std::ostringstream inner;
            inner << "Last: " << last.quantity << " @ "
                  << std::fixed << std::setprecision(2) << last.price
                  << " (Trade #" << last.trade_id << ")";
            std::string content = inner.str();

            oss << "  |  " << content;
            // Pad to 37 chars total inner width (matches other rows).
            for (size_t i = content.size() + 2; i < 39; ++i) oss << ' ';
            oss << "|\n";

            oss << "  |  Total Trades: " << std::setw(6) << tradeLog_.size()
                << "                |\n";
        } else {
            oss << "  |  No trades executed yet.             |\n";
        }
    }

    oss << "  +=======================================+\n";
    return oss.str();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Matching Engine (runs on dedicated background thread)
// ═══════════════════════════════════════════════════════════════════════════════

void OrderBook::matchingEngineLoop() {
    while (running_.load(std::memory_order_acquire)) {
        std::queue<Order> batch;

        // ── Phase 1: Wait for incoming orders ──
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCV_.wait(lock, [this] {
                return !incomingQueue_.empty()
                    || !running_.load(std::memory_order_acquire);
            });

            if (!running_.load(std::memory_order_acquire)
                && incomingQueue_.empty()) {
                break;  // Clean exit.
            }

            // Drain the entire incoming queue into a local batch.
            // This minimizes time spent holding queueMutex_.
            std::swap(batch, incomingQueue_);
        }

        // ── Phase 2: Insert orders into the book & match ──
        {
            std::lock_guard<std::mutex> lock(bookMutex_);

            while (!batch.empty()) {
                Order order = std::move(batch.front());
                batch.pop();

                if (order.side == Side::BUY) {
                    bids_.insert(order);
                } else {
                    asks_.insert(order);
                }
            }

            // Attempt to match crossing orders.
            tryMatch();
        }
    }
}

void OrderBook::tryMatch() {
    /*
     * Matching loop: continuously check if the best bid crosses the best ask.
     *
     * A "cross" occurs when:    bestBid.price >= bestAsk.price
     *
     * Execution price convention:
     *   The resting order's price is used. The resting order is the one that
     *   was already in the book (arrived earlier). When both arrive in the
     *   same batch, we use the earlier timestamp.
     *
     * Partial fills:
     *   If the matched orders have different quantities, the smaller side
     *   is fully filled. The remaining quantity of the larger order is
     *   re-inserted into the book with its original timestamp preserved
     *   (maintaining time priority).
     */
    while (!bids_.empty() && !asks_.empty()) {
        auto bidIt = bids_.begin();
        auto askIt = asks_.begin();

        // No match if best bid < best ask — the book is uncrossed.
        if (bidIt->price < askIt->price) {
            break;
        }

        // ── Determine execution price ──
        // The resting (earlier) order's price is used.
        double execPrice = (bidIt->timestamp <= askIt->timestamp)
                           ? bidIt->price
                           : askIt->price;

        // ── Determine fill quantity ──
        uint32_t execQty = std::min(bidIt->quantity, askIt->quantity);

        // ── Create trade record ──
        Trade trade;
        trade.trade_id      = nextTradeId_.fetch_add(1, std::memory_order_relaxed);
        trade.buy_order_id  = bidIt->id;
        trade.sell_order_id = askIt->id;
        trade.price         = execPrice;
        trade.quantity       = execQty;
        trade.timestamp     = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> tLock(tradeMutex_);
            tradeLog_.push_back(trade);
        }

        std::cout << "[OrderBook] MATCH " << trade.toString() << "\n";

        // ── Update or remove filled orders ──
        //
        // std::set elements are const (immutable keys), so we must
        // extract, modify, and re-insert for partial fills.
        uint32_t bidRemaining = bidIt->quantity - execQty;
        uint32_t askRemaining = askIt->quantity - execQty;

        Order bidOrder = *bidIt;
        Order askOrder = *askIt;

        bids_.erase(bidIt);
        asks_.erase(askIt);

        // Re-insert partially filled orders with updated quantities.
        // Original timestamps are preserved to maintain time priority.
        if (bidRemaining > 0) {
            bidOrder.quantity = bidRemaining;
            bids_.insert(bidOrder);
        }
        if (askRemaining > 0) {
            askOrder.quantity = askRemaining;
            asks_.insert(askOrder);
        }
    }
}

} // namespace apex
