/**
 * @file order.h
 * @brief core data structures for the apexmatch order matching engine.
 * @author Abhijeet Senapati
 *
 * defines the foundational types used throughout the engine:
 * -side enum (buy/sell)
 * -order struct (limit order representation)
 * -trade struct (executed fill record)
 * -bidcomparator/askcomparator (price-time priority ordering)
 */

#pragma once
#include <cstdint>
#include <chrono>
#include <string>
#include <sstream>
#include <iomanip>

namespace apex {

using namespace std;

//enums
/**
 * @enum side
 * @brief represents the side of an order in the market.
 */
enum class Side : uint8_t {
    BUY,   ///< bid side — the buyer wants to purchase.
    SELL   ///< ask side — the seller wants to sell.
};

/**
 * @brief converts a side enum to its string representation.
 * @param side the order side.
 * @return "buy" or "sell".
 */
inline const char* sideToString(Side side) {
    return side == Side::BUY ? "BUY" : "SELL";
}

//order
/**
 * @struct order
 * @brief represents a single limit order submitted to the exchange.
 *
 * each order is assigned a monotonically-increasing unique id at submission
 * time, along with a high-resolution timestamp used for price-time priority
 * resolution when multiple orders share the same limit price.
 */
struct Order {
    uint64_t    id;         ///< unique order identifier (monotonically increasing).
    Side        side;       ///< buy or sell.
    double      price;      ///< limit price for the order.
    uint32_t    quantity;   ///< number of shares / contracts.

    ///high-resolution arrival timestamp for time-priority tiebreaking.
    chrono::steady_clock::time_point timestamp;

    /**
     * @brief formats the order as a human-readable string.
     * @return e.g. "[order #42 buy 100 @ 150.25]"
     */
    string toString() const {
        ostringstream oss;
        oss << "[Order #" << id << " " << sideToString(side)
            << " " << quantity << " @ "
            << fixed << setprecision(2) << price << "]";
        return oss.str();
    }
};

//trade
/**
 * @struct trade
 * @brief records a completed trade (fill) between two crossing orders.
 *
 * a trade is emitted by the matching engine whenever a buy order's price
 * meets or exceeds a sell order's price. the execution price follows the
 * resting (earlier-arriving) order's limit price.
 */
struct Trade {
    uint64_t    trade_id;       ///< unique trade identifier.
    uint64_t    buy_order_id;   ///< id of the buy-side order.
    uint64_t    sell_order_id;  ///< id of the sell-side order.
    double      price;          ///< execution price.
    uint32_t    quantity;       ///< filled quantity.

    /// timestamp of trade execution.
    chrono::steady_clock::time_point timestamp;

    /**
     * @brief formats the trade as a human-readable string.
     * @return e.g. "[trade #1 buyorder#3 x sellorder#7 | 50 @ 150.25]"
     */
    string toString() const {
        ostringstream oss;
        oss << "[Trade #" << trade_id
            << " BuyOrder#" << buy_order_id
            << " x SellOrder#" << sell_order_id
            << " | " << quantity << " @ "
            << fixed << setprecision(2) << price << "]";
        return oss.str();
    }
};

//comparators (price-time priority)
/**
 * @struct bidcomparator
 * @brief strict weak ordering for the buy side (max-heap behavior).
 *
 * sorting rules (price-time priority):
 *   1. highest price first (descending price).
 *   2. earliest timestamp first (ascending time) — older orders have priority.
 *   3. lowest order id as final tiebreaker (ensures strict weak ordering).
 *
 * when used with set, begin() always points to the best bid:
 * the highest-priced, earliest-arriving buy order.
 */
struct BidComparator {
    bool operator()(const Order& a, const Order& b) const {
        if (a.price != b.price)         return a.price > b.price;        // higher price wins.
        if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp; // earlier time wins.
        return a.id < b.id;                                               // id tiebreaker.
    }
};

/**
 * @struct askcomparator
 * @brief strict weak ordering for the sell side (min-heap behavior).
 *
 * sorting rules (price-time priority):
 *   1. lowest price first (ascending price).
 *   2. earliest timestamp first (ascending time) — older orders have priority.
 *   3. lowest order id as final tiebreaker (ensures strict weak ordering).
 *
 * when used with set, begin() always points to the best ask:
 * the lowest-priced, earliest-arriving sell order.
 */
struct AskComparator {
    bool operator()(const Order& a, const Order& b) const {
        if (a.price != b.price)         return a.price < b.price;        // lower price wins.
        if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp; // earlier time wins.
        return a.id < b.id;                                               // id tiebreaker.
    }
};

} //namespace apex
