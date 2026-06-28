/**
 * @file orderbook.cpp
 * @brief implementation of the thread-safe limit order book.
 * @author Abhijeet Senapati
 *
 * implementation details:
 *   - the matching engine runs on a dedicated background thread, consuming
 *     incoming orders in batches to minimize lock contention.
 *   - trymatch() implements aggressive matching: it loops until the best
 *     bid no longer crosses the best ask.
 *   - partial fills are supported: when two orders cross but have different
 *     quantities, the smaller side is fully filled and the larger side's
 *     remaining quantity is re-inserted into the book.
 *   - trade execution price follows the resting order (the one that arrived
 *     earlier), which is the standard exchange convention.
 */

#include "OrderBook.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

using namespace std;

namespace apex {

//construction/destruction
OrderBook::OrderBook(const string& symbol)
    : symbol_(symbol)
    , running_(true)
{
    //launch the dedicated matching engine thread.
    matchingThread_ = thread(&OrderBook::matchingEngineLoop, this);
    cout << "[OrderBook] Matching engine started for "<<symbol_<< ".\n";
}

OrderBook::~OrderBook() {
    shutdown();
}

void OrderBook::shutdown() {
    //atomically flip running_ from true to false exactly once.
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;  //already shut down—idempotent.
    }

    queueCV_.notify_all();  //wake the engine thread so it can exit.

    if (matchingThread_.joinable()) {
        matchingThread_.join();
    }

    cout << "[OrderBook] Matching engine stopped for "<<symbol_<< ".\n";
}

//order submission (called from client threads)

uint64_t OrderBook::submitOrder(Side side, double price, uint32_t quantity) {
    Order order;
    order.id        = nextOrderId_.fetch_add(1, memory_order_relaxed);
    order.side      = side;
    order.price     = price;
    order.quantity   = quantity;
    order.timestamp = chrono::steady_clock::now();

    //enqueue the order for the matching engine.
    {
        lock_guard<mutex> lock(queueMutex_);
        incomingQueue_.push(order);
    }

    //signal the matching engine that work is available.
    queueCV_.notify_one();

    cout << "[OrderBook] Queued " << order.toString() << "\n";
    return order.id;
}
//view(called from client threads)

string OrderBook::viewTopOfBook() const {
    lock_guard<mutex> lock(bookMutex_);

    ostringstream oss;
    oss << "\n";
    oss << "  +=======================================+\n";
    oss << "  |       Order Book: " << symbol_;
    //pad to align the right border
    for (size_t i = symbol_.size(); i < 20; ++i) oss << ' ';
    oss << "|\n";
    oss << "  +=======================================+\n";

    //best bid
    if (!bids_.empty()) {
        const auto& best = *bids_.begin();
        oss << "  |  Best Bid:  "
            << fixed << setprecision(2) << setw(10) << best.price
            << " x " << setw(6) << best.quantity
            << "     |\n";
    } else {
        oss << "  |  Best Bid:        ---                |\n";
    }

    //best ask
    if (!asks_.empty()) {
        const auto& best = *asks_.begin();
        oss << "  |  Best Ask:  "
            << fixed << setprecision(2) << setw(10) << best.price
            << " x " << setw(6) << best.quantity
            << "     |\n";
    } else {
        oss << "  |  Best Ask:        ---                |\n";
    }

    //spread
    oss << "  +---------------------------------------+\n";
    if (!bids_.empty() && !asks_.empty()) {
        double spread = asks_.begin()->price - bids_.begin()->price;
        oss << "  |  Spread:    "
            << fixed << setprecision(2) << setw(10) << spread
            << "              |\n";
    } else {
        oss << "  |  Spread:          N/A                |\n";
    }

    oss << "  |  Bid Depth: " << setw(10) << bids_.size()
        << " orders       |\n";
    oss << "  |  Ask Depth: " << setw(10) << asks_.size()
        << " orders       |\n";

    //last trade
    oss << "  +---------------------------------------+\n";
    {
        lock_guard<mutex> tLock(tradeMutex_);
        if (!tradeLog_.empty()) {
            const auto& last = tradeLog_.back();
            //build the inner content, then pad to fixed width
            ostringstream inner;
            inner << "Last: " << last.quantity << " @ "
                  << fixed << setprecision(2) << last.price
                  << " (Trade #" << last.trade_id << ")";
            string content = inner.str();
            oss << "  |  " << content;
            //pad to 37 chars total inner width (matches other rows)
            for (size_t i = content.size() + 2; i < 39; ++i) oss << ' ';
            oss << "|\n";
            oss << "  |  Total Trades: " << setw(6) << tradeLog_.size()
                << "                |\n";
        } else {
            oss << "  |  No trades executed yet.             |\n";
        }
    }
    oss << "  +=======================================+\n";
    return oss.str();
}

//matching engine (runs on dedicated background thread)


void OrderBook::matchingEngineLoop() {
    while (running_.load(memory_order_acquire)) {
        queue<Order> batch;

        //phase 1:wait for incoming orders
        {
            unique_lock<mutex> lock(queueMutex_);
            queueCV_.wait(lock, [this] {
                return !incomingQueue_.empty()
                    || !running_.load(memory_order_acquire);
            });
            if (!running_.load(memory_order_acquire)
                && incomingQueue_.empty()) {
                break;  //clean exit
            }
            //drain the entire incoming queue into a local batch
            //this minimizes time spent holding queuemutex_
            swap(batch, incomingQueue_);
        }

        //phase 2: insert orders into the book & match
        {
            lock_guard<mutex> lock(bookMutex_);
            while (!batch.empty()) {
                Order order = move(batch.front());
                batch.pop();

                if (order.side == Side::BUY) {
                    bids_.insert(order);
                } else {
                    asks_.insert(order);
                }
            }
            //attempt to match crossing orders
            tryMatch();
        }
    }
}

void OrderBook::tryMatch() {
    /*
     * matching loop: continuously check if the best bid crosses the best ask.
     * a "cross" occurs when:    bestbid.price >= bestask.price
     * execution price convention:
     *   the resting order's price is used. the resting order is the one that
     *   was already in the book (arrived earlier). when both arrive in the
     *   same batch, we use the earlier timestamp.
     * partial fills:
     *   if the matched orders have different quantities, the smaller side
     *   is fully filled. the remaining quantity of the larger order is
     *   re-inserted into the book with its original timestamp preserved
     *   (maintaining time priority).
     */
    while (!bids_.empty() && !asks_.empty()) {
        auto bidIt = bids_.begin();
        auto askIt = asks_.begin();
        //no match if best bid < best ask—the book is uncrossed
        if (bidIt->price < askIt->price) {
            break;
        }
        //---determine execution price
        //the resting (earlier) order's price is used.
        double execPrice = (bidIt->timestamp <= askIt->timestamp)
                           ? bidIt->price
                           : askIt->price;

        //---determine fill quantity
        uint32_t execQty = min(bidIt->quantity, askIt->quantity);

        //---create trade record
        Trade trade;
        trade.trade_id      = nextTradeId_.fetch_add(1, memory_order_relaxed);
        trade.buy_order_id  = bidIt->id;
        trade.sell_order_id = askIt->id;
        trade.price         = execPrice;
        trade.quantity       = execQty;
        trade.timestamp     = chrono::steady_clock::now();
        {
            lock_guard<mutex> tLock(tradeMutex_);
            tradeLog_.push_back(trade);
        }
        cout << "[OrderBook] MATCH " << trade.toString() << "\n";

        //update or remove filled orders
        //set elements are const(immutable keys),so we must
        //extract,modify,and re-insert for partial fills.
        uint32_t bidRemaining = bidIt->quantity - execQty;
        uint32_t askRemaining = askIt->quantity - execQty;

        Order bidOrder = *bidIt;
        Order askOrder = *askIt;

        bids_.erase(bidIt);
        asks_.erase(askIt);

        //re-insert partially filled orders with updated quantities.
        //original timestamps are preserved to maintain time priority.
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

}//namespace apex
