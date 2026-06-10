<div align="center">

# ⚡ LLQTSimPlatform

### Low-Latency Quantitative Trading Simulation Platform

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue?style=for-the-badge&logo=cplusplus)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.22%2B-blue?style=for-the-badge&logo=cmake)](https://cmake.org/)
[![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey?style=for-the-badge)](https://github.com)
[![Tests](https://img.shields.io/badge/Tests-40%2B-brightgreen?style=for-the-badge)](https://github.com)

*A modular, high-performance C++20 framework for backtesting and simulating quantitative trading strategies with nanosecond-precision execution, institutional-grade risk management, and full portfolio analytics.*

</div>

---

## ✨ Highlights

| Feature | Detail |
|---|---|
| 🚀 **Ultra-Low Latency** | Order matching `<1µs`, signal generation `<100ns` |
| 📊 **Market Data Replay** | Memory-mapped binary stores, 1M+ ticks/sec throughput |
| 🧠 **3 Built-in Strategies** | SMA crossover, Momentum (ROC+EMA), Mean Reversion (Z-score) |
| 🛡️ **Pre-trade Risk Checks** | Position limits, VaR, drawdown, daily loss, kill switch |
| 🔬 **Research Tools** | Grid search parameter scans, walk-forward validation |
| 📈 **Rich Reporting** | CSV export, self-contained HTML reports with Chart.js |
| 🔌 **Networking Layer** | TCP/UDP market feeds with binary framing + CRC32 |
| ✅ **40+ Unit Tests** | Full GoogleTest coverage across all modules |

---

## 🏛️ Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Reporting Layer                             │
│              CSV Writer · HTML Reports · CLI · Config               │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────────────┐
│                          Research Layer                             │
│            BacktestResult · ParameterScan · WalkForward             │
└──────┬───────────────────────────────────────────────────┬──────────┘
       │                                                   │
┌──────▼──────────┐  ┌────────────────────────┐  ┌────────▼──────────┐
│    Analytics    │  │      Risk Engine        │  │     Portfolio     │
│ Benchmarks · σ  │  │  VaR · Limits · HWM    │  │ Position · PnL   │
└──────┬──────────┘  └───────────┬────────────┘  └────────┬──────────┘
       └───────────────────────┬─┘                        │
                               │◄─────────────────────────┘
┌──────────────────────────────▼──────────────────────────────────────┐
│                       Core Trading Layer                            │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────────┐   │
│  │    Exchange      │  │    Strategy     │  │   Market Data    │   │
│  │  OrderBook       │  │  SMA · Momentum │  │  Types · Store   │   │
│  │  MatchingEngine  │  │  MeanReversion  │  │  ReplayEngine    │   │
│  │  ExecutionSim    │  │  CRTP Base      │  │  DataPortal      │   │
│  └─────────────────┘  └─────────────────┘  └──────────────────┘   │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────────────┐
│                      Infrastructure Layer                           │
│          TCP Feeds · UDP Feeds · Binary Protocol · Thread Pool       │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 📦 Project Structure

```
llqts_platform/
├── 📄 CMakeLists.txt              # Unified root build (9 libraries + CLI + 40 tests)
├── 📄 README.md
│
├── market_data/                   # Foundation types & storage
│   ├── include/market_data/
│   │   ├── types.hpp              # Tick, Bar, fixed-point arithmetic
│   │   ├── binary_store.hpp       # mmap-backed flat binary data store
│   │   ├── data_portal.hpp        # Point-in-time data access (no-lookahead)
│   │   └── replay_engine.hpp      # Historical data replay + subscriptions
│   └── src/ & tests/
│
├── networking/                    # Market data feeds & wire protocol
│   ├── include/networking/
│   │   ├── binary_protocol.hpp    # CRC32 framed binary wire format
│   │   ├── tcp_feed.hpp           # Async TCP feed handler
│   │   └── udp_feed.hpp           # Async UDP feed handler
│   └── src/ & tests/
│
├── exchange/                      # Order matching & execution simulation
│   ├── include/exchange/
│   │   ├── order_book.hpp         # Price/time priority limit order book
│   │   ├── matching_engine.hpp    # Matching logic (Market, Limit, IOC, FOK)
│   │   ├── execution_sim.hpp      # Fill simulation with slippage models
│   │   └── order_router.hpp       # Order routing & lifecycle management
│   └── src/ & tests/
│
├── strategy/                      # Trading strategies (CRTP interface)
│   ├── include/strategy/
│   │   ├── strategy_base.hpp      # CRTP IStrategy interface
│   │   ├── sma.hpp                # SMA crossover (golden/death cross)
│   │   ├── momentum.hpp           # Rate-of-change + EMA smoothing
│   │   └── mean_reversion.hpp     # Rolling Z-score with Bollinger logic
│   └── src/ & tests/
│
├── portfolio/                     # Position tracking & performance
├── risk/                          # Pre-trade risk checks & VaR
├── analytics/                     # Statistical analysis & benchmarking
├── research/                      # Parameter scans & walk-forward
├── reporting/                     # CSV, HTML, CLI output
└── event_driven_core/             # (Experimental) lock-free event bus
```

---

## 🔧 Building

### Requirements

- **C++20 compiler** — GCC 10+, Clang 12+, or MSVC 2019+
- **CMake 3.22+**
- Internet access (to fetch GoogleTest via CMake FetchContent)

---

### 🐧 Linux / macOS

```bash
git clone https://github.com/yourusername/llqts_platform.git
cd llqts_platform

mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 🪟 Windows (MSVC)

```powershell
mkdir build && cd build

# Configure
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release ..

# Build
cmake --build . --config Release -j

# Run tests
ctest -C Release -V
```

### 🪟 Windows (MinGW)

```powershell
mkdir build && cd build
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..
mingw32-make -j4
```

---

### Build Output

```
build/
├── bin/    ← llqts_cli, test_* executables
└── lib/    ← libllqts_market_data.a, libllqts_exchange.a, ...
```

---

## 🧪 Testing

Run all tests:

```bash
cd build
ctest -V --output-on-failure
```

Run a specific module's tests:

```bash
ctest -R "exchange" -V           # Exchange tests only
ctest -R "portfolio" -V          # Portfolio tests only
ctest -R "test_sma" -V           # Single test binary
```

| Module | Test Targets |
|---|---|
| `market_data` | `test_binary_store`, `test_replay_engine`, `test_data_portal` |
| `networking` | `test_binary_protocol`, `test_tcp_feed`, `test_udp_feed` |
| `exchange` | `test_order_book`, `test_matching_engine`, `test_execution_sim`, `test_order_router` |
| `strategy` | `test_sma`, `test_momentum`, `test_mean_reversion` |
| `portfolio` | `test_portfolio`, `test_performance` |
| `risk` | `test_risk_engine`, `test_var_engine` |
| `analytics` | `test_benchmark`, `test_stats` |
| `research` | `test_parameter_scan`, `test_walk_forward` |
| `reporting` | `test_csv_writer`, `test_html_report`, `test_cli`, `test_config` |

---

## 🚀 Quick Start

### Basic Backtest Loop

```cpp
#include "market_data/replay_engine.hpp"
#include "exchange/execution_sim.hpp"
#include "strategy/sma.hpp"
#include "portfolio/portfolio.hpp"

int main() {
    // Load historical tick data
    BinaryStore<md::Tick> store("AAPL.ticks", md::StoreMode::ReadOnly);
    HistoricalReplayEngine replay(store.records(), {
        .start_ts        = 0,
        .end_ts          = std::numeric_limits<md::Timestamp>::max(),
        .speed_multiplier = 0.0,   // max speed
    });

    strat::SMAStrategy sma({.symbol_id = 1, .fast_period = 20, .slow_period = 50});
    port::Portfolio    portfolio(1'000'000.0);  // $1M capital

    // Replay ticks → aggregate into bars → feed strategy
    replay.subscribe([&](const md::Tick& tick) {
        // ... aggregate tick into bar, call sma.on_bar(bar) ...
    });

    while (auto tick = replay.step()) {}

    auto perf = portfolio.performance();
    std::cout << "Sharpe Ratio : " << perf.sharpe_ratio()  << "\n"
              << "Max Drawdown : " << perf.max_drawdown()  << "\n"
              << "Total Return : " << perf.total_return()  << "\n";
}
```

### Custom Strategy

```cpp
#include "strategy/strategy_base.hpp"

namespace strat {

class MyStrategy : public IStrategy {
    std::deque<double> prices_;

public:
    void on_bar(const md::Bar& bar) override {
        prices_.push_back(md::from_price(bar.close));
        if (prices_.size() > 100) prices_.pop_front();

        if (should_buy())  emit_signal(Signal::BUY);
        if (should_sell()) emit_signal(Signal::SELL);
    }

    void on_tick(const md::Tick&) override {}
    void reset()                  override { prices_.clear(); }
    const char* name() const noexcept override { return "MyStrategy"; }

private:
    bool should_buy()  const { /* your logic */ return false; }
    bool should_sell() const { /* your logic */ return false; }
};

} // namespace strat
```

### Parameter Scanning

```cpp
#include "research/parameter_scan.hpp"
#include "strategy/sma.hpp"

int main() {
    res::ParameterScan scan;

    // Define parameter grid
    for (int fast : {10, 15, 20})
    for (int slow : {40, 50, 60}) {
        scan.add_run({{"fast", fast}, {"slow", slow}});
    }

    auto result = res::run_scan(scan, [](const auto& params) -> res::BacktestResult {
        // Run your backtest with these params and return metrics
        return {};
    });

    result.sort_by_sharpe();
    if (auto* best = result.best_by_sharpe()) {
        std::cout << "Best Sharpe: " << best->sharpe_ratio() << "\n";
    }
}
```

---

## 📐 Modules & Namespaces

| Module | Namespace | Key Classes |
|---|---|---|
| `market_data` | `md` | `Tick`, `Bar`, `BinaryStore<T>`, `ReplayEngine`, `DataPortal` |
| `networking` | `net` | `TcpFeed`, `UdpFeed`, `BinaryParser`, `BinaryProtocol` |
| `exchange` | `exch` | `OrderBook`, `MatchingEngine`, `ExecutionSim`, `OrderRouter` |
| `strategy` | `strat` | `IStrategy`, `SMAStrategy`, `MomentumStrategy`, `MeanReversionStrategy` |
| `portfolio` | `port` | `Position`, `Portfolio`, `Performance`, `EquityCurve` |
| `risk` | `risk` | `RiskEngine`, `VarEngine`, `RiskLimits`, `BreachEvent` |
| `analytics` | `anlt` | `BenchmarkResult`, `Stats`, `Timer` |
| `research` | `res` | `BacktestResult`, `ParameterScan`, `WalkForward`, `ScanResult` |
| `reporting` | `rpt` | `CsvWriter`, `HtmlReport`, `Cli`, `Config` |

---

## ⚡ Performance Characteristics

### Latency

| Operation | Latency |
|---|---|
| Order book update | `< 1 µs` |
| Strategy signal generation | `< 100 ns` |
| Portfolio fill update | `< 1 µs` |
| Market data replay throughput | `1M+ ticks/sec` |

### Design Decisions

- **Fixed-point arithmetic** — Prices stored as `int64_t × 1e8` (no float rounding)
- **`alignas(64)` structs** — `Tick` cache-line aligned for SIMD-friendly access
- **`mmap` data store** — Zero-copy historical data replay via memory-mapped files
- **Lock-free queues** — MPSC queue in event-driven core for high-throughput
- **CRTP strategies** — Zero-overhead polymorphism via static dispatch

---

## 🛡️ Risk Management

The `RiskEngine` performs **synchronous pre-trade checks** before any order is submitted:

```
✅ Kill switch (emergency halt)
✅ Max order quantity
✅ Max order notional value
✅ Max position size per symbol
✅ Max gross portfolio exposure
✅ Max net portfolio exposure
✅ Max daily loss (resets per session)
✅ Max drawdown from high-water mark
✅ 95% Value-at-Risk limit
```

Breach callbacks fire immediately when any limit is first crossed, enabling real-time alerting.

---

## 🔨 Build Configurations

```bash
# Release (default) — O3 + SIMD
cmake -DCMAKE_BUILD_TYPE=Release ..

# Debug — ASan + UBSan + debug symbols
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Custom SIMD target
cmake -DCMAKE_CXX_FLAGS="-march=znver3 -O3" ..
```

---

## 🗺️ Roadmap

- [ ] GPU-accelerated backtesting (CUDA)
- [ ] Machine learning integration (TensorFlow/LibTorch)
- [ ] Live trading support (broker API integration)
- [ ] Real-time monitoring dashboard
- [ ] Distributed backtesting (multi-machine)
- [ ] Python bindings (PyBind11)
- [ ] Event-driven core integration into main pipeline

---

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Write code and tests
4. Ensure all tests pass: `ctest -V`
5. Submit a pull request

**Style guide:** `snake_case` for functions/variables · `PascalCase` for classes · 4-space indent · 100-char line limit

---

## 📄 License

[MIT License](LICENSE) — feel free to use in research and commercial projects.

---

<div align="center">

**Built with ❤️ for quants, by quants.**

*Happy trading! 🚀📈*

</div>
