#pragma once
// reporting/config.hpp  ── Lightweight YAML-subset configuration parser
//
// Supports a strict subset of YAML:
//   key: value
//   section:
//     key: value
//
// No anchors, no sequences, no multi-document.
// Values are stored as strings; typed accessors parse on demand.

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rpt {

// ── Config ────────────────────────────────────────────────────────────────────
class Config {
public:
    /// Load from a YAML file on disk.  Returns false on I/O error.
    [[nodiscard]] bool load_file(std::string_view path);

    /// Parse from a YAML string in memory.
    [[nodiscard]] bool parse(std::string_view yaml);

    /// Dump current config back to a YAML string.
    [[nodiscard]] std::string dump() const;

    // ── Typed accessors ───────────────────────────────────────────────────────

    /// Get a raw string value at "section.key" or "key".
    [[nodiscard]] std::optional<std::string>
    get_string(std::string_view key) const noexcept;

    [[nodiscard]] std::optional<double>
    get_double(std::string_view key) const noexcept;

    [[nodiscard]] std::optional<int64_t>
    get_int(std::string_view key) const noexcept;

    [[nodiscard]] std::optional<bool>
    get_bool(std::string_view key) const noexcept;

    /// Get a value with a default fallback
    [[nodiscard]] std::string
    get_string(std::string_view key, std::string_view dflt) const noexcept;

    [[nodiscard]] double  get_double(std::string_view key, double  dflt) const noexcept;
    [[nodiscard]] int64_t get_int   (std::string_view key, int64_t dflt) const noexcept;
    [[nodiscard]] bool    get_bool  (std::string_view key, bool    dflt) const noexcept;

    /// Set a key (section.key or top-level key)
    void set(std::string_view key, std::string value);

    [[nodiscard]] bool has(std::string_view key) const noexcept;
    [[nodiscard]] bool empty() const noexcept { return store_.empty(); }

    /// All keys in insertion order
    [[nodiscard]] const std::unordered_map<std::string, std::string>&
    all() const noexcept { return store_; }

private:
    std::unordered_map<std::string, std::string> store_;  // flat key→value
};

} // namespace rpt
