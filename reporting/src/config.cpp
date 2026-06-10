// reporting/config.cpp  ── YAML-subset config parser
#include "reporting/config.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <string>

namespace rpt {

// ── Internal helpers ──────────────────────────────────────────────────────────
static std::string trim(std::string_view sv) {
    const std::string_view ws = " \t\r\n";
    const auto b = sv.find_first_not_of(ws);
    if (b == std::string_view::npos) return {};
    const auto e = sv.find_last_not_of(ws);
    return std::string(sv.substr(b, e - b + 1));
}

// ── parse ─────────────────────────────────────────────────────────────────────
bool Config::parse(std::string_view yaml) {
    store_.clear();
    std::string current_section;
    std::istringstream ss{std::string(yaml)};
    std::string line;

    while (std::getline(ss, line)) {
        // Strip inline comment
        const auto comment = line.find('#');
        if (comment != std::string::npos)
            line.erase(comment);

        if (line.empty() || trim(line).empty()) continue;

        // Detect indentation depth to identify section vs key
        const std::size_t indent = line.find_first_not_of(' ');
        const std::string_view content = std::string_view(line).substr(indent);

        const auto colon = content.find(':');
        if (colon == std::string_view::npos) continue;

        std::string key   = trim(content.substr(0, colon));
        std::string value = trim(content.substr(colon + 1));

        if (value.empty()) {
            // Possible section header — only at zero indent
            if (indent == 0) {
                current_section = key;
            }
            continue;
        }

        // Strip YAML quotes
        if (value.size() >= 2
            && ((value.front() == '"' && value.back() == '"')
             || (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }

        const std::string flat_key = (indent > 0 && !current_section.empty())
            ? current_section + "." + key
            : key;

        store_[flat_key] = std::move(value);
    }
    return true;
}

// ── load_file ─────────────────────────────────────────────────────────────────
bool Config::load_file(std::string_view path) {
    std::ifstream f{std::string(path)};
    if (!f.is_open()) return false;
    const std::string content{std::istreambuf_iterator<char>(f),
                              std::istreambuf_iterator<char>()};
    return parse(content);
}

// ── dump ──────────────────────────────────────────────────────────────────────
std::string Config::dump() const {
    std::ostringstream out;
    // Group by section prefix
    for (const auto& [k, v] : store_) {
        const auto dot = k.find('.');
        if (dot == std::string::npos) {
            out << k << ": " << v << '\n';
        } else {
            out << k.substr(0, dot) << ":\n"
                << "  " << k.substr(dot + 1) << ": " << v << '\n';
        }
    }
    return out.str();
}

// ── Accessors ─────────────────────────────────────────────────────────────────
std::optional<std::string>
Config::get_string(std::string_view key) const noexcept {
    auto it = store_.find(std::string(key));
    if (it == store_.end()) return std::nullopt;
    return it->second;
}

std::optional<double>
Config::get_double(std::string_view key) const noexcept {
    auto s = get_string(key);
    if (!s) return std::nullopt;
    try { return std::stod(*s); } catch (...) { return std::nullopt; }
}

std::optional<int64_t>
Config::get_int(std::string_view key) const noexcept {
    auto s = get_string(key);
    if (!s) return std::nullopt;
    int64_t v{};
    auto [ptr, ec] = std::from_chars(s->data(), s->data() + s->size(), v);
    if (ec != std::errc{}) return std::nullopt;
    return v;
}

std::optional<bool>
Config::get_bool(std::string_view key) const noexcept {
    auto s = get_string(key);
    if (!s) return std::nullopt;
    if (*s == "true"  || *s == "yes" || *s == "1") return true;
    if (*s == "false" || *s == "no"  || *s == "0") return false;
    return std::nullopt;
}

std::string Config::get_string(std::string_view key,
                                std::string_view dflt) const noexcept {
    return get_string(key).value_or(std::string(dflt));
}

double Config::get_double(std::string_view key, double dflt) const noexcept {
    return get_double(key).value_or(dflt);
}

int64_t Config::get_int(std::string_view key, int64_t dflt) const noexcept {
    return get_int(key).value_or(dflt);
}

bool Config::get_bool(std::string_view key, bool dflt) const noexcept {
    return get_bool(key).value_or(dflt);
}

void Config::set(std::string_view key, std::string value) {
    store_[std::string(key)] = std::move(value);
}

bool Config::has(std::string_view key) const noexcept {
    return store_.contains(std::string(key));
}

} // namespace rpt
