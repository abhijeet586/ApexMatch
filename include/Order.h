/**
 * @file Order.h
 * @brief Core data structures for the ApexMatch Order Matching Engine.
 * @author Abhijeet Senapati
 *
 * Defines the foundational types used throughout the engine:
 *   - Side enum (BUY / SELL)
 *   - Order struct (limit order representation)
 *   - Trade struct (executed fill record)
 *   - BidComparator / AskComparator (Price-Time Priority ordering)
 */

#pragma once

#include <cstdint>
#include <chrono>
#include <string>
#include <sstream>
#include <iomanip>

namespace apex {

// ═══════════════════════════════════════════════════════════════════════════════
//  Enums
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @enum Side
 * @brief Represents the side of an order in the market.
 */
enum class Side : uint8_t {
    BUY,   ///< Bid side — the buyer wants to purchase.
    SELL   ///< Ask side — the seller wants to sell.
};

/**
 * @brief Converts a Side enum to its string representation.
 * @param side The order side.
 * @return "BUY" or "SELL".
 */
inline const char* sideToString(Side side) {
    return side == Side::BUY ? "BUY" : "SELL";
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Order
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @struct Order
 * @brief Represents a single limit order submitted to the exchange.
 *
 * Each order is assigned a monotonically-increasing unique ID at submission
 * time, along with a high-resolution timestamp used for Price-Time Priority
 * resolution when multiple orders share the same limit price.
 */
struct Order {
    uint64_t    id;         ///< Unique order identifier (monotonically increasing).
    Side        side;       ///< BUY or SELL.
    double      price;      ///< Limit price for the order.
    uint32_t    quantity;   ///< Number of shares / contracts.

    /// High-resolution arrival timestamp for time-priority tiebreaking.
    std::chrono::steady_clock::time_point timestamp;

    /**
     * @brief Formats the order as a human-readable string.
     * @return e.g. "[Order #42 BUY 100 @ 150.25]"
     */
    std::string toString() const {
        std::ostringstream oss;
        oss << "[Order #" << id << " " << sideToString(side)
            << " " << quantity << " @ "
            << std::fixed << std::setprecision(2) << price << "]";
        return oss.str();
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
//  Trade
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @struct Trade
 * @brief Records a completed trade (fill) between two crossing orders.
 *
 * A Trade is emitted by the matching engine whenever a buy order's price
 * meets or exceeds a sell order's price. The execution price follows the
 * resting (earlier-arriving) order's limit price.
 */
struct Trade {
    uint64_t    trade_id;       ///< Unique trade identifier.
    uint64_t    buy_order_id;   ///< ID of the buy-side order.
    uint64_t    sell_order_id;  ///< ID of the sell-side order.
    double      price;          ///< Execution price.
    uint32_t    quantity;       ///< Filled quantity.

    /// Timestamp of trade execution.
    std::chrono::steady_clock::time_point timestamp;

    /**
     * @brief Formats the trade as a human-readable string.
     * @return e.g. "[Trade #1 BuyOrder#3 x SellOrder#7 | 50 @ 150.25]"
     */
    std::string toString() const {
        std::ostringstream oss;
        oss << "[Trade #" << trade_id
            << " BuyOrder#" << buy_order_id
            << " x SellOrder#" << sell_order_id
            << " | " << quantity << " @ "
            << std::fixed << std::setprecision(2) << price << "]";
        return oss.str();
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
//  Comparators (Price-Time Priority)
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @struct BidComparator
 * @brief Strict weak ordering for the Buy side (Max-Heap behavior).
 *
 * Sorting rules (Price-Time Priority):
 *   1. Highest price first (descending price).
 *   2. Earliest timestamp first (ascending time) — older orders have priority.
 *   3. Lowest order ID as final tiebreaker (ensures strict weak ordering).
 *
 * When used with std::set, begin() always points to the best bid:
 * the highest-priced, earliest-arriving buy order.
 */
struct BidComparator {
    bool operator()(const Order& a, const Order& b) const {
        if (a.price != b.price)         return a.price > b.price;        // Higher price wins.
        if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp; // Earlier time wins.
        return a.id < b.id;                                               // ID tiebreaker.
    }
};

/**
 * @struct AskComparator
 * @brief Strict weak ordering for the Sell side (Min-Heap behavior).
 *
 * Sorting rules (Price-Time Priority):
 *   1. Lowest price first (ascending price).
 *   2. Earliest timestamp first (ascending time) — older orders have priority.
 *   3. Lowest order ID as final tiebreaker (ensures strict weak ordering).
 *
 * When used with std::set, begin() always points to the best ask:
 * the lowest-priced, earliest-arriving sell order.
 */
struct AskComparator {
    bool operator()(const Order& a, const Order& b) const {
        if (a.price != b.price)         return a.price < b.price;        // Lower price wins.
        if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp; // Earlier time wins.
        return a.id < b.id;                                               // ID tiebreaker.
    }
};

} // namespace apex
