// reporting/cli.cpp
#include "reporting/cli.hpp"

#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace rpt {

// ── ParsedArgs ────────────────────────────────────────────────────────────────
bool ParsedArgs::flag(std::string_view name) const noexcept {
    auto it = flags_.find(std::string(name));
    return (it != flags_.end()) && it->second;
}

std::optional<std::string>
ParsedArgs::option(std::string_view name) const noexcept {
    auto it = options_.find(std::string(name));
    if (it == options_.end()) return std::nullopt;
    return it->second;
}

std::string ParsedArgs::option(std::string_view name,
                                std::string_view dflt) const noexcept {
    return option(name).value_or(std::string(dflt));
}

// ── CliParser ─────────────────────────────────────────────────────────────────
CliParser::CliParser(std::string prog_name, std::string description)
    : prog_name_(std::move(prog_name)), description_(std::move(description))
{}

void CliParser::flag(std::string long_name, std::string short_name,
                      std::string description) {
    flags_.push_back({std::move(long_name), std::move(short_name),
                      std::move(description)});
}

void CliParser::option(std::string long_name, std::string short_name,
                        std::string meta_var, std::string description) {
    options_.push_back({std::move(long_name), std::move(short_name),
                        std::move(meta_var),  std::move(description)});
}

ParsedArgs CliParser::parse(int argc, const char* const* argv) const {
    ParsedArgs result;

    for (int i = 1; i < argc; ) {
        const std::string tok = argv[i];

        // Check flags
        bool matched = false;
        for (const auto& f : flags_) {
            if (tok == f.lng || tok == f.shrt) {
                result.flags_[f.lng] = true;
                matched = true;
                ++i;
                break;
            }
        }
        if (matched) continue;

        // Check options
        for (const auto& o : options_) {
            if (tok == o.lng || tok == o.shrt) {
                if (i + 1 >= argc) {
                    result.errors_.push_back("Option '" + tok + "' requires an argument");
                    ++i;
                    matched = true;
                    break;
                }
                result.options_[o.lng] = argv[i + 1];
                i += 2;
                matched = true;
                break;
            }
        }
        if (matched) continue;

        // Treat as positional
        result.positional_.push_back(tok);
        ++i;
    }
    return result;
}

void CliParser::print_help() const {
    std::cout << prog_name_;
    if (!description_.empty()) std::cout << " — " << description_;
    std::cout << "\n\nUsage:\n  " << prog_name_ << " [options] [args...]\n\nOptions:\n";

    for (const auto& f : flags_)
        std::cout << "  " << std::left << std::setw(20)
                  << (f.shrt + ", " + f.lng) << "  " << f.desc << '\n';

    for (const auto& o : options_)
        std::cout << "  " << std::left << std::setw(20)
                  << (o.shrt + ", " + o.lng + " <" + o.meta + ">")
                  << "  " << o.desc << '\n';
    std::cout << '\n';
}

} // namespace rpt
