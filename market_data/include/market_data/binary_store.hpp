#pragma once
// market_data/binary_store.hpp  ── mmap-backed flat binary data store
//
// Design:
//  • A BinaryStore<T> maps a regular file into virtual memory using mmap(2).
//  • All records are trivially-copyable, fixed-size, stored contiguously.
//  • Supports O(log N) timestamp-based point-in-time lookup via lower_bound.
//  • Writer path appends records with msync() for durability.
//  • RAII: the mapping is released and the fd is closed on destruction.
//  • Thread-safety: read-only access is fully concurrent; writes are
//    serialised by the caller (single-producer pattern).

#include "market_data/types.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

// POSIX / Linux headers – only included on Linux
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace md {

// ── Open mode ────────────────────────────────────────────────────────────────
enum class StoreMode {
    ReadOnly,   ///< MAP_SHARED | PROT_READ
    ReadWrite,  ///< MAP_SHARED | PROT_READ | PROT_WRITE, can append
    Create,     ///< Create new file + ReadWrite
};

// ── File header (always the first sizeof(StoreHeader) bytes) ─────────────────
struct StoreHeader {
    uint32_t magic;       ///< 0xDEAD'BEF0
    uint32_t version;     ///< Format version (currently 1)
    uint64_t record_size; ///< sizeof(T) at write time
    uint64_t record_count;///< Number of records written
    uint8_t  _pad[40];    ///< Reserved, pads header to 64 bytes
};
static_assert(sizeof(StoreHeader) == 64);
static_assert(std::is_trivially_copyable_v<StoreHeader>);

inline constexpr uint32_t kStoreMagic   = 0xDEADBEF0u;
inline constexpr uint32_t kStoreVersion = 1u;

// ── BinaryStore<T> ────────────────────────────────────────────────────────────
template<MarketRecord T>
class BinaryStore {
public:
    using value_type      = T;
    using size_type       = std::size_t;
    using const_reference = const T&;
    using pointer         = T*;
    using const_pointer   = const T*;

    // ── Iterators ─────────────────────────────────────────────────────────────
    class const_iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = T;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const T*;
        using reference         = const T&;

        const_iterator() noexcept = default;
        explicit const_iterator(const T* ptr) noexcept : ptr_(ptr) {}

        reference         operator*()  const noexcept { return *ptr_; }
        pointer           operator->() const noexcept { return ptr_;  }
        const_iterator&   operator++() noexcept { ++ptr_; return *this; }
        const_iterator    operator++(int) noexcept { auto tmp=*this; ++(*this); return tmp; }
        const_iterator&   operator--() noexcept { --ptr_; return *this; }
        const_iterator    operator--(int) noexcept { auto tmp=*this; --(*this); return tmp; }
        const_iterator    operator+(difference_type n) const noexcept { return const_iterator{ptr_+n}; }
        const_iterator    operator-(difference_type n) const noexcept { return const_iterator{ptr_-n}; }
        difference_type   operator-(const_iterator o) const noexcept { return ptr_-o.ptr_; }
        const_iterator&   operator+=(difference_type n) noexcept { ptr_+=n; return *this; }
        const_iterator&   operator-=(difference_type n) noexcept { ptr_-=n; return *this; }
        reference         operator[](difference_type n) const noexcept { return ptr_[n]; }
        auto              operator<=>(const const_iterator&) const noexcept = default;

    private:
        const T* ptr_{nullptr};
    };

    using iterator = const_iterator; // Store is always at least const from outside

    // ── Construction / destruction ────────────────────────────────────────────
    BinaryStore() noexcept = default;

    /// Open an existing file (ReadOnly or ReadWrite) or create a new one.
    explicit BinaryStore(const std::filesystem::path& path, StoreMode mode)
    {
        open(path, mode);
    }

    BinaryStore(const BinaryStore&)            = delete;
    BinaryStore& operator=(const BinaryStore&) = delete;

    BinaryStore(BinaryStore&& o) noexcept
        : fd_(o.fd_), map_ptr_(o.map_ptr_), map_len_(o.map_len_)
        , header_(o.header_), mode_(o.mode_)
    {
        o.fd_      = -1;
        o.map_ptr_ = nullptr;
        o.map_len_ = 0;
        o.header_  = nullptr;
    }

    BinaryStore& operator=(BinaryStore&& o) noexcept {
        if (this != &o) { close_store(); std::swap(*this, o); }
        return *this;
    }

    ~BinaryStore() noexcept { close_store(); }

    // ── Open / close ─────────────────────────────────────────────────────────
    void open(const std::filesystem::path& path, StoreMode mode);
    void close_store() noexcept;

    // ── Capacity ─────────────────────────────────────────────────────────────
    [[nodiscard]] size_type size()  const noexcept {
        return header_ ? header_->record_count : 0;
    }
    [[nodiscard]] bool      empty() const noexcept { return size() == 0; }

    // ── Element access ────────────────────────────────────────────────────────
    [[nodiscard]] const T& operator[](size_type i) const noexcept {
        return data_ptr()[i];
    }
    [[nodiscard]] const T& at(size_type i) const {
        if (i >= size()) throw std::out_of_range("BinaryStore::at out of range");
        return data_ptr()[i];
    }

    /// Zero-copy view of all records
    [[nodiscard]] std::span<const T> records() const noexcept {
        return {data_ptr(), size()};
    }

    // ── Iterators ─────────────────────────────────────────────────────────────
    [[nodiscard]] const_iterator begin()  const noexcept { return const_iterator{data_ptr()}; }
    [[nodiscard]] const_iterator end()    const noexcept { return const_iterator{data_ptr() + size()}; }
    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] const_iterator cend()   const noexcept { return end(); }

    // ── Point-in-time lookup (no lookahead) ───────────────────────────────────
    /// Returns iterator to first record with recv_ts >= ts.
    /// Equivalent to std::lower_bound on recv_ts field – O(log N).
    [[nodiscard]] const_iterator lower_bound(Timestamp ts) const noexcept;

    /// Returns the last record with recv_ts <= ts, or nullopt if none.
    /// Enforces no-lookahead: caller supplies current simulation time.
    [[nodiscard]] std::optional<T> last_at_or_before(Timestamp ts) const noexcept;

    // ── Write interface (ReadWrite / Create mode only) ────────────────────────
    void append(const T& record);
    void flush() noexcept; ///< msync to disk

    // ── Diagnostics ──────────────────────────────────────────────────────────
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

private:
    int          fd_      {-1};
    void*        map_ptr_ {nullptr};
    std::size_t  map_len_ {0};
    StoreHeader* header_  {nullptr};
    StoreMode    mode_    {StoreMode::ReadOnly};

    static constexpr std::size_t kInitialCapacity = 1024; // records

    [[nodiscard]] T*       data_ptr()       noexcept {
        return reinterpret_cast<T*>(
            static_cast<std::byte*>(map_ptr_) + sizeof(StoreHeader));
    }
    [[nodiscard]] const T* data_ptr() const noexcept {
        return reinterpret_cast<const T*>(
            static_cast<const std::byte*>(map_ptr_) + sizeof(StoreHeader));
    }

    void remap(std::size_t new_file_size);
};

// ── Explicit instantiation declarations ──────────────────────────────────────
extern template class BinaryStore<Tick>;
extern template class BinaryStore<Bar>;

} // namespace md
