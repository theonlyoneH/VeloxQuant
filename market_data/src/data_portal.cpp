// market_data/data_portal.cpp  ── DataPortal implementation

#include "market_data/data_portal.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <stdexcept>

namespace md {

// ── Directory constructor ─────────────────────────────────────────────────────
DataPortal::DataPortal(const std::filesystem::path& data_dir,
                       Timestamp start_ts, Timestamp end_ts)
    : current_ts_(start_ts)
{
    for (const auto& entry : std::filesystem::directory_iterator(data_dir)) {
        if (!entry.is_regular_file()) continue;
        const auto& p = entry.path();
        if (p.extension() != ".ticks") continue;

        const std::string ticker = p.stem().string();
        BinaryStore<Tick> store(p, StoreMode::ReadOnly);
        register_store(ticker, std::move(store));
    }
    (void)end_ts; // Used by callers for replay engine; stored if needed in future
}

// ── register_store (auto id) ─────────────────────────────────────────────────
SymbolId DataPortal::register_store(const std::string& ticker,
                                    BinaryStore<Tick>  store) {
    if (ticker_map_.contains(ticker))
        throw std::invalid_argument("DataPortal: ticker already registered: " + ticker);

    const SymbolId id = next_id_++;
    ticker_map_[ticker] = id;
    by_id_.emplace(id, SymbolData{ticker, std::move(store)});
    return id;
}

// ── register_store (explicit id) ─────────────────────────────────────────────
void DataPortal::register_store(SymbolId id, const std::string& ticker,
                                BinaryStore<Tick> store) {
    if (by_id_.contains(id))
        throw std::invalid_argument("DataPortal: id already registered");
    if (ticker_map_.contains(ticker))
        throw std::invalid_argument("DataPortal: ticker already registered: " + ticker);

    ticker_map_[ticker] = id;
    by_id_.emplace(id, SymbolData{ticker, std::move(store)});
    if (id >= next_id_) next_id_ = id + 1;
}

// ── advance_to ────────────────────────────────────────────────────────────────
void DataPortal::advance_to(Timestamp ts) noexcept {
    // Enforce monotonic time – never go backwards
    if (ts > current_ts_) current_ts_ = ts;
}

// ── current_tick ─────────────────────────────────────────────────────────────
std::optional<Tick> DataPortal::current_tick(SymbolId id) const noexcept {
    auto it = by_id_.find(id);
    if (it == by_id_.end()) return std::nullopt;
    return it->second.store.last_at_or_before(current_ts_);
}

std::optional<Tick> DataPortal::current_tick(std::string_view ticker) const noexcept {
    auto id = lookup_id(ticker);
    if (!id) return std::nullopt;
    return current_tick(*id);
}

// ── current_price ─────────────────────────────────────────────────────────────
std::optional<double> DataPortal::current_price(SymbolId id) const noexcept {
    auto t = current_tick(id);
    if (!t) return std::nullopt;
    // Prefer mid-price; fall back to last
    if (t->bid_price > 0 && t->ask_price > 0)
        return from_price((t->bid_price + t->ask_price) / 2);
    return from_price(t->last_price);
}

// ── history (raw ticks) ───────────────────────────────────────────────────────
std::vector<Tick> DataPortal::history(SymbolId id, Timestamp lookback_ns) const {
    auto it = by_id_.find(id);
    if (it == by_id_.end()) return {};

    const auto& store = it->second.store;
    const Timestamp from_ts = current_ts_ - lookback_ns;

    // Find first record >= from_ts
    auto begin_it = store.lower_bound(from_ts);
    // Find first record > current_ts_ (no lookahead)
    auto end_it   = store.lower_bound(current_ts_ + 1);

    return std::vector<Tick>(begin_it, end_it);
}

// ── bar_duration_ns ───────────────────────────────────────────────────────────
int64_t DataPortal::bar_duration_ns(BarFrequency freq) const noexcept {
    constexpr int64_t kSec  = 1'000'000'000LL;
    constexpr int64_t kMin  = 60LL  * kSec;
    constexpr int64_t kHour = 3600LL * kSec;
    constexpr int64_t kDay  = 86400LL * kSec;

    switch (freq.type) {
        case BarType::Second: return static_cast<int64_t>(freq.count) * kSec;
        case BarType::Minute: return static_cast<int64_t>(freq.count) * kMin;
        case BarType::Hour:   return static_cast<int64_t>(freq.count) * kHour;
        case BarType::Day:    return static_cast<int64_t>(freq.count) * kDay;
        default:              return static_cast<int64_t>(freq.count) * kSec;
    }
}

// ── build_bars ────────────────────────────────────────────────────────────────
std::vector<Bar> DataPortal::build_bars(std::span<const Tick> ticks,
                                        BarFrequency freq,
                                        std::size_t n_bars) const {
    if (ticks.empty()) return {};

    const int64_t dur = bar_duration_ns(freq);
    std::vector<Bar> bars;
    bars.reserve(n_bars);

    // Determine first bar's open timestamp aligned to bar boundary
    const Timestamp first_ts = ticks.front().recv_ts;
    Timestamp bar_open = (first_ts / dur) * dur;

    Bar cur{};
    bool in_bar = false;
    Price   sum_pv = 0; // price * volume accumulator for VWAP
    Quantity vol   = 0;

    auto finalise = [&]() {
        if (!in_bar) return;
        cur.vwap   = (vol > 0) ? Price(sum_pv / static_cast<int64_t>(vol)) : cur.close;
        cur.volume = vol;
        bars.push_back(cur);
        // Keep only n_bars most recent
        if (bars.size() > n_bars) bars.erase(bars.begin());
        in_bar = false;
        sum_pv = 0;
        vol    = 0;
    };

    for (const auto& tick : ticks) {
        // No-lookahead enforcement: skip future ticks
        if (tick.recv_ts > current_ts_) break;

        const Timestamp bar_close = bar_open + dur;
        if (tick.recv_ts >= bar_close) {
            finalise();
            // Advance bar window
            bar_open = (tick.recv_ts / dur) * dur;
        }

        const Price px = (tick.last_price > 0) ? tick.last_price : tick.bid_price;
        const Quantity q = tick.last_size;

        if (!in_bar) {
            cur = Bar{};
            cur.recv_ts   = bar_open + dur - 1;
            cur.open_ts   = bar_open;
            cur.close_ts  = bar_open + dur - 1;
            cur.symbol_id = tick.symbol_id;
            cur.open      = px;
            cur.high      = px;
            cur.low       = px;
            cur.close     = px;
            cur.trade_count = 0;
            in_bar = true;
        } else {
            cur.high  = std::max(cur.high,  px);
            cur.low   = std::min(cur.low,   px);
            cur.close = px;
        }

        if (q > 0) {
            sum_pv += px * static_cast<int64_t>(q);
            vol    += q;
        }
        ++cur.trade_count;
    }
    finalise();

    return bars;
}

// ── history_bars ─────────────────────────────────────────────────────────────
std::vector<Bar> DataPortal::history_bars(SymbolId id, BarFrequency freq,
                                          std::size_t n_bars) const {
    const int64_t lookback = static_cast<int64_t>(n_bars + 1) * bar_duration_ns(freq);
    const auto ticks = history(id, lookback);
    return build_bars(ticks, freq, n_bars);
}

// ── subscribe ─────────────────────────────────────────────────────────────────
auto DataPortal::subscribe(SymbolId id, TickCallback cb) -> SubHandle {
    auto it = by_id_.find(id);
    if (it == by_id_.end())
        throw std::invalid_argument("DataPortal::subscribe: unknown symbol id");

    SubHandle h = next_handle_++;
    subscriptions_.push_back({id, h, std::move(cb)});
    return h;
}

auto DataPortal::subscribe(std::string_view ticker, TickCallback cb) -> SubHandle {
    auto id = lookup_id(ticker);
    if (!id) throw std::invalid_argument(
        "DataPortal::subscribe: unknown ticker " + std::string(ticker));
    return subscribe(*id, std::move(cb));
}

// ── unsubscribe ───────────────────────────────────────────────────────────────
void DataPortal::unsubscribe(SubHandle h) {
    subscriptions_.erase(
        std::remove_if(subscriptions_.begin(), subscriptions_.end(),
            [h](const PortalSub& s){ return s.portal_handle == h; }),
        subscriptions_.end());
}

// ── lookup helpers ────────────────────────────────────────────────────────────
std::optional<SymbolId> DataPortal::lookup_id(std::string_view ticker) const noexcept {
    auto it = ticker_map_.find(std::string(ticker));
    if (it == ticker_map_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> DataPortal::lookup_ticker(SymbolId id) const noexcept {
    auto it = by_id_.find(id);
    if (it == by_id_.end()) return std::nullopt;
    return it->second.ticker;
}

std::vector<SymbolId> DataPortal::all_symbols() const {
    std::vector<SymbolId> ids;
    ids.reserve(by_id_.size());
    for (const auto& [id, _] : by_id_) ids.push_back(id);
    return ids;
}

} // namespace md
