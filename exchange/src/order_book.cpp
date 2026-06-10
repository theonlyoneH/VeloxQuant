// exchange/order_book.cpp  ── L2 order book implementation

#include "exchange/order_book.hpp"

#include <algorithm>
#include <stdexcept>

namespace exch {

// ── Construction ──────────────────────────────────────────────────────────────
OrderBook::OrderBook(SymbolId symbol_id) noexcept
    : symbol_id_(symbol_id)
{}

// ── add ───────────────────────────────────────────────────────────────────────
void OrderBook::add(Order& order) {
    // Compute queue position: shares ahead at same price level
    if (order.side == Side::Buy) {
        auto* q = bids_.orders_at(order.limit_price);
        uint64_t ahead = 0;
        if (q) {
            for (OrderId oid : *q) {
                auto it = orders_.find(oid);
                if (it != orders_.end())
                    ahead += it->second.leaves_qty();
            }
        }
        order.queue_pos = ahead;
        bids_.add_order(order.limit_price, order.id);
    } else {
        auto* q = asks_.orders_at(order.limit_price);
        uint64_t ahead = 0;
        if (q) {
            for (OrderId oid : *q) {
                auto it = orders_.find(oid);
                if (it != orders_.end())
                    ahead += it->second.leaves_qty();
            }
        }
        order.queue_pos = ahead;
        asks_.add_order(order.limit_price, order.id);
    }

    order_side_[order.id] = order.side;
    orders_[order.id]     = order;   // Store a copy
}

// ── cancel ────────────────────────────────────────────────────────────────────
bool OrderBook::cancel(OrderId id) {
    auto side_it = order_side_.find(id);
    if (side_it == order_side_.end()) return false;

    auto ord_it = orders_.find(id);
    if (ord_it == orders_.end()) return false;

    Order& ord = ord_it->second;
    const Price p = ord.limit_price;

    if (side_it->second == Side::Buy)
        bids_.remove_order(p, id);
    else
        asks_.remove_order(p, id);

    ord.status = OrderStatus::Cancelled;
    order_side_.erase(side_it);
    orders_.erase(ord_it);
    return true;
}

// ── reduce_qty ────────────────────────────────────────────────────────────────
void OrderBook::reduce_qty(OrderId id, Quantity executed_qty) {
    auto it = orders_.find(id);
    if (it == orders_.end()) return;

    Order& ord     = it->second;
    ord.filled_qty = std::min(ord.filled_qty + executed_qty, ord.qty);

    if (ord.filled_qty >= ord.qty) {
        // Fully filled – remove from book structure but leave in orders_ briefly
        // so MatchingEngine can read the final state before we erase it
        const Price p    = ord.limit_price;
        const Side  side = ord.side;

        if (side == Side::Buy)
            bids_.remove_order(p, id);
        else
            asks_.remove_order(p, id);

        ord.status = OrderStatus::Filled;
        order_side_.erase(id);
    } else {
        ord.status = OrderStatus::PartiallyFilled;
    }
}

// ── best_bid / best_ask ───────────────────────────────────────────────────────
std::optional<Price> OrderBook::best_bid() const noexcept {
    return bids_.best_price();
}
std::optional<Price> OrderBook::best_ask() const noexcept {
    return asks_.best_price();
}

std::optional<Quantity> OrderBook::best_bid_qty() const noexcept {
    if (bids_.levels.empty()) return std::nullopt;
    const auto& q = bids_.levels.begin()->second;
    Quantity total = 0;
    for (OrderId oid : q) {
        auto it = orders_.find(oid);
        if (it != orders_.end()) total += it->second.leaves_qty();
    }
    return total;
}

std::optional<Quantity> OrderBook::best_ask_qty() const noexcept {
    if (asks_.levels.empty()) return std::nullopt;
    const auto& q = asks_.levels.begin()->second;
    Quantity total = 0;
    for (OrderId oid : q) {
        auto it = orders_.find(oid);
        if (it != orders_.end()) total += it->second.leaves_qty();
    }
    return total;
}

// ── mid_price ────────────────────────────────────────────────────────────────
std::optional<Price> OrderBook::mid_price() const noexcept {
    auto b = best_bid();
    auto a = best_ask();
    if (!b || !a) return std::nullopt;
    return (*b + *a) / 2;
}

std::optional<Price> OrderBook::spread() const noexcept {
    auto b = best_bid();
    auto a = best_ask();
    if (!b || !a) return std::nullopt;
    return *a - *b;
}

// ── depth ────────────────────────────────────────────────────────────────────
namespace {
template<typename Compare>
std::vector<PriceLevel> build_depth(const BookSide<Compare>& side,
                                    const std::unordered_map<OrderId, Order>& orders,
                                    std::size_t n) {
    std::vector<PriceLevel> result;
    result.reserve(n);
    for (const auto& [price, queue] : side.levels) {
        if (result.size() >= n) break;
        PriceLevel lvl{};
        lvl.price       = price;
        lvl.order_count = static_cast<uint32_t>(queue.size());
        for (OrderId oid : queue) {
            auto it = orders.find(oid);
            if (it != orders.end()) lvl.total_qty += it->second.leaves_qty();
        }
        result.push_back(lvl);
    }
    return result;
}
} // anon namespace

std::vector<PriceLevel> OrderBook::bid_depth(std::size_t n) const {
    return build_depth(bids_, orders_, n);
}
std::vector<PriceLevel> OrderBook::ask_depth(std::size_t n) const {
    return build_depth(asks_, orders_, n);
}

// ── queue_position ────────────────────────────────────────────────────────────
uint64_t OrderBook::queue_position(OrderId id) const noexcept {
    auto side_it = order_side_.find(id);
    if (side_it == order_side_.end()) return 0;

    auto ord_it = orders_.find(id);
    if (ord_it == orders_.end()) return 0;

    const Price p    = ord_it->second.limit_price;
    const Side  side = side_it->second;

    if (side == Side::Buy)
        return shares_ahead(bids_, p, id);
    else
        return shares_ahead(asks_, p, id);
}

template<typename Compare>
uint64_t OrderBook::shares_ahead(const BookSide<Compare>& bs,
                                  Price price, OrderId target_id) const noexcept {
    const auto* q = bs.orders_at(price);
    if (!q) return 0;
    uint64_t ahead = 0;
    for (OrderId oid : *q) {
        if (oid == target_id) break;
        auto it = orders_.find(oid);
        if (it != orders_.end()) ahead += it->second.leaves_qty();
    }
    return ahead;
}

// Explicit instantiations for the two comparators used
template uint64_t OrderBook::shares_ahead<std::greater<Price>>(
    const BookSide<std::greater<Price>>&, Price, OrderId) const noexcept;
template uint64_t OrderBook::shares_ahead<std::less<Price>>(
    const BookSide<std::less<Price>>&, Price, OrderId) const noexcept;

// ── find ─────────────────────────────────────────────────────────────────────
const Order* OrderBook::find(OrderId id) const noexcept {
    auto it = orders_.find(id);
    return (it != orders_.end()) ? &it->second : nullptr;
}
Order* OrderBook::find_mut(OrderId id) noexcept {
    auto it = orders_.find(id);
    return (it != orders_.end()) ? &it->second : nullptr;
}

} // namespace exch
