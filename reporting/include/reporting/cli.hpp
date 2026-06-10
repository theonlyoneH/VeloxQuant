#pragma once
// reporting/cli.hpp  ── Command-line interface argument parser
//
// Minimal positional + flag + option parser.  No external libraries.
//
// Usage:
//   CliParser cli("llqts_cli", "Low-Latency QT Simulation Platform");
//   cli.flag("--help",    "-h", "Print help");
//   cli.option("--config","-c", "PATH", "Config file path");
//   cli.option("--output","-o", "DIR",  "Output directory");
//   auto args = cli.parse(argc, argv);
//   if (args.flag("--help")) { cli.print_help(); return 0; }
//   auto cfg_path = args.option("--config");

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rpt {

// ── ParsedArgs ────────────────────────────────────────────────────────────────
struct ParsedArgs {
    [[nodiscard]] bool flag(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::string> option(std::string_view name) const noexcept;
    [[nodiscard]] std::string option(std::string_view name,
                                     std::string_view dflt) const noexcept;
    [[nodiscard]] const std::vector<std::string>& positional() const noexcept {
        return positional_;
    }
    [[nodiscard]] bool has_errors() const noexcept { return !errors_.empty(); }
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept {
        return errors_;
    }

    // ── Internal (set by CliParser) ──────────────────────────────────────────
    std::unordered_map<std::string, bool>        flags_;
    std::unordered_map<std::string, std::string> options_;
    std::vector<std::string>                     positional_;
    std::vector<std::string>                     errors_;
};

// ── CliParser ─────────────────────────────────────────────────────────────────
class CliParser {
public:
    explicit CliParser(std::string prog_name, std::string description = "");

    /// Register a boolean flag (present = true)
    void flag(std::string long_name, std::string short_name,
              std::string description);

    /// Register an option that takes one string argument
    void option(std::string long_name, std::string short_name,
                std::string meta_var, std::string description);

    /// Parse argc/argv; argv[0] is the program name and is skipped.
    [[nodiscard]] ParsedArgs parse(int argc, const char* const* argv) const;

    /// Print formatted help to stdout.
    void print_help() const;

private:
    struct FlagDef  { std::string lng, shrt, desc; };
    struct OptionDef{ std::string lng, shrt, meta, desc; };

    std::string prog_name_;
    std::string description_;
    std::vector<FlagDef>   flags_;
    std::vector<OptionDef> options_;
};

} // namespace rpt
