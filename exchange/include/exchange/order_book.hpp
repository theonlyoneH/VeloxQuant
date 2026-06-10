#pragma once
// exchange/order_book.hpp  ── Level-2 order book with price-time priority
//
// Design:
//  • Two sides: bids (descending price) and asks (ascending price).
//  • Each price level holds a std::deque<OrderId> preserving FIFO (time priority).
//  • Orders are owned by the caller (OrderRouter); the book stores only OrderId
//    references plus a flat lookup table for O(1) order metadata access.
//  • add() / cancel() / modify_qty() are O(log P) where P = number of price levels.
//  • best_bid() / best_ask() are O(1) via iterators cached on the std::map ends.
//  • depth(n) sweeps the top-N levels for slippage estimation.
//  • queue_position(id) counts shares ahead of the order at its price level.
//  • The book is NOT thread-safe; driven by a single simulation thread.

#include "exchange/types.hpp"

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace exch {

// ── Side view (one half of the book) ─────────────────────────────────────────
// Bids use std::greater<Price> → highest price first.
// Asks use std::less<Price>    → lowest price first.

template<typename Compare>
struct BookSide {
    using LevelMap = std::map<Price, std::deque<OrderId>, Compare>;

    LevelMap levels;  ///< price → FIFO queue of order IDs

    void add_order(Price p, OrderId id) {
        levels[p].push_back(id);
    }

    /// Remove a specific order from a price level.
    /// Returns true if the level was erased (became empty).
    bool remove_order(Price p, OrderId id) {
        auto it = levels.find(p);
        if (it == levels.end()) return false;
        auto& q = it->second;
        for (auto qi = q.begin(); qi != q.end(); ++qi) {
            if (*qi == id) { q.erase(qi); break; }
        }
        if (q.empty()) { levels.erase(it); return true; }
        return false;
    }

    [[nodiscard]] bool empty() const noexcept { return levels.empty(); }

    /// Best price (front of map due to comparator)
    [[nodiscard]] std::optional<Price> best_price() const noexcept {
        if (levels.empty()) return std::nullopt;
        return levels.begin()->first;
    }

    /// All orders at a given price level (FIFO order)
    [[nodiscard]] const std::deque<OrderId>*
    orders_at(Price p) const noexcept {
        auto it = levels.find(p);
        if (it == levels.end()) return nullptr;
        return &it->second;
    }

    std::deque<OrderId>* orders_at_mut(Price p) noexcept {
        auto it = levels.find(p);
        if (it == levels.end()) return nullptr;
        return &it->second;
    }
};

// ── OrderBook ─────────────────────────────────────────────────────────────────
class OrderBook {
public:
    explicit OrderBook(SymbolId symbol_id) noexcept;

    // ── Order lifecycle ───────────────────────────────────────────────────────

    /// Insert a resting order into the book.
    /// Updates order.queue_pos with estimated shares ahead.
    /// Caller must ensure order.status == New and it is a passive (limit/stop) order.
    void add(Order& order);

    /// Remove an order entirely.  Sets order.status = Cancelled.
    bool cancel(OrderId id);

    /// Reduce the leaf quantity of a resting order (partial fill update).
    /// Does NOT remove the order from its position in the queue.
    void reduce_qty(OrderId id, Quantity executed_qty);

    // ── Queries ───────────────────────────────────────────────────────────────

    [[nodiscard]] std::optional<Price>    best_bid()  const noexcept;
    [[nodiscard]] std::optional<Price>    best_ask()  const noexcept;
    [[nodiscard]] std::optional<Quantity> best_bid_qty() const noexcept;
    [[nodiscard]] std::optional<Quantity> best_ask_qty() const noexcept;

    /// Midpoint price (average of best bid and ask), or nullopt if one side empty.
    [[nodiscard]] std::optional<Price> mid_price() const noexcept;

    /// Spread in fixed-point price units.
    [[nodiscard]] std::optional<Price> spread() const noexcept;

    /// Top-N price levels per side (for depth display / slippage estimation).
    [[nodiscard]] std::vector<PriceLevel> bid_depth(std::size_t n) const;
    [[nodiscard]] std::vector<PriceLevel> ask_depth(std::size_t n) const;

    /// Estimated number of shares ahead of `id` at its price level.
    /// Returns 0 if order not found or already at front.
    [[nodiscard]] uint64_t queue_position(OrderId id) const noexcept;

    /// Lookup a resting order (returns nullptr if not found).
    [[nodiscard]] const Order* find(OrderId id) const noexcept;
    [[nodiscard]]       Order* find_mut(OrderId id)  noexcept;

    [[nodiscard]] SymbolId symbol_id()   const noexcept { return symbol_id_; }
    [[nodiscard]] std::size_t bid_levels() const noexcept { return bids_.levels.size(); }
    [[nodiscard]] std::size_t ask_levels() const noexcept { return asks_.levels.size(); }
    [[nodiscard]] std::size_t order_count() const noexcept { return orders_.size(); }

    // ── Matching engine access ─────────────────────────────────────────────────
    // The MatchingEngine needs mutable access to consume from front of levels.
    BookSide<std::greater<Price>>& bids() noexcept { return bids_; }
    BookSide<std::less<Price>>&    asks() noexcept { return asks_; }
    std::unordered_map<OrderId, Order>& orders() noexcept { return orders_; }
    const std::unordered_map<OrderId, Order>& orders() const noexcept { return orders_; }

private:
    SymbolId                            symbol_id_;
    BookSide<std::greater<Price>>       bids_;   ///< Descending: highest bid first
    BookSide<std::less<Price>>          asks_;   ///< Ascending:  lowest ask first
    std::unordered_map<OrderId, Order>  orders_; ///< id → Order (owns order state)

    // Per-order side tag for O(1) cancel routing
    std::unordered_map<OrderId, Side>   order_side_;

    // Helper: compute total qty at a price level for queue_pos
    template<typename Compare>
    uint64_t shares_ahead(const BookSide<Compare>& side,
                          Price price, OrderId target_id) const noexcept;
};

} // namespace exch
