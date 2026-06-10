// research/backtest_result.cpp
#include "research/backtest_result.hpp"
#include "portfolio/performance.hpp"

namespace res {

void compute(BacktestResult& r, double risk_free_rate, double periods_per_year) {
    r.metrics = port::compute_metrics(r.equity_curve, risk_free_rate, periods_per_year);

    if (r.total_trades > 0) {
        r.win_rate = static_cast<double>(r.winning_trades)
                   / static_cast<double>(r.total_trades);
    }

    const double abs_losses = (r.avg_loss < 0.0) ? -r.avg_loss : 0.0;
    if (abs_losses > 0.0 && r.winning_trades > 0 && r.losing_trades > 0) {
        const double gross_win  = r.avg_win  * static_cast<double>(r.winning_trades);
        const double gross_loss = abs_losses  * static_cast<double>(r.losing_trades);
        r.profit_factor = gross_win / gross_loss;
    }
}

} // namespace res
