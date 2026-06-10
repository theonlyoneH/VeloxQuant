// research/walk_forward.cpp
#include "research/walk_forward.hpp"
#include "portfolio/performance.hpp"

namespace res {

// ── make_windows ──────────────────────────────────────────────────────────────
std::vector<WalkForwardWindow>
make_windows(const WalkForwardConfig& cfg, std::size_t total_bars) {
    std::vector<WalkForwardWindow> wins;
    if (total_bars < cfg.train_bars + cfg.test_bars) return wins;

    std::size_t fold = 0;
    for (std::size_t start = 0;
         start + cfg.train_bars + cfg.test_bars <= total_bars;
         start += cfg.step_bars) {
        WalkForwardWindow w{};
        w.train_start = start;
        w.train_end   = start + cfg.train_bars;
        w.test_start  = w.train_end;
        w.test_end    = w.test_start + cfg.test_bars;
        w.fold_idx    = fold++;
        wins.push_back(w);
    }
    return wins;
}

// ── WalkForwardResult ─────────────────────────────────────────────────────────
port::EquityCurve WalkForwardResult::stitched_equity() const {
    port::EquityCurve curve;
    double base_nav = 0.0;
    int64_t ts_offset = 0;

    for (const auto& r : oos_results) {
        if (r.equity_curve.empty()) continue;

        if (curve.empty()) {
            base_nav   = r.equity_curve.front().nav;
            ts_offset  = 0;
        }

        const double scale = (base_nav > 0.0)
            ? (curve.empty() ? 1.0 : curve.back().nav / r.equity_curve.front().nav)
            : 1.0;

        const int64_t t_start = r.equity_curve.front().timestamp;
        for (const auto& pt : r.equity_curve) {
            curve.push_back({pt.timestamp - t_start + ts_offset,
                             pt.nav * scale});
        }
        ts_offset = curve.back().timestamp + 1;
    }
    return curve;
}

PerformanceMetrics WalkForwardResult::aggregate_metrics(
        double risk_free_rate, double periods_per_year) const {
    auto curve = stitched_equity();
    return port::compute_metrics(curve, risk_free_rate, periods_per_year);
}

// ── run_walk_forward ──────────────────────────────────────────────────────────
WalkForwardResult run_walk_forward(const WalkForwardConfig& cfg,
                                    std::size_t total_bars,
                                    TrainFn train_fn,
                                    TestFn  test_fn) {
    WalkForwardResult wfr;
    wfr.windows = make_windows(cfg, total_bars);

    wfr.oos_results.reserve(wfr.windows.size());
    for (const auto& w : wfr.windows) {
        auto params = train_fn(w.train_start, w.train_end);
        auto result = test_fn(params, w.test_start, w.test_end);
        wfr.oos_results.push_back(std::move(result));
    }
    return wfr;
}

} // namespace res
