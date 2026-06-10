// market_data/binary_store.cpp  ── mmap-backed flat binary data store

#include "market_data/binary_store.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

// POSIX
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace md {

namespace {
// Round up to nearest page boundary
std::size_t page_align(std::size_t n) noexcept {
    const std::size_t page = static_cast<std::size_t>(::getpagesize());
    return (n + page - 1) & ~(page - 1);
}

// Compute required file size for N records
template<typename T>
std::size_t file_size_for(std::size_t n) noexcept {
    return sizeof(StoreHeader) + n * sizeof(T);
}

// Helper: throw errno-based exception
[[noreturn]] void throw_errno(const char* ctx) {
    throw std::system_error(errno, std::generic_category(), ctx);
}
} // anon namespace

// ── open ─────────────────────────────────────────────────────────────────────
template<MarketRecord T>
void BinaryStore<T>::open(const std::filesystem::path& path, StoreMode mode) {
    if (is_open()) throw std::logic_error("BinaryStore already open");
    mode_ = mode;

    int oflags = 0;
    int prot   = PROT_READ;
    int mflags = MAP_SHARED;

    if (mode == StoreMode::ReadOnly) {
        oflags = O_RDONLY;
    } else if (mode == StoreMode::ReadWrite) {
        oflags = O_RDWR;
        prot  |= PROT_WRITE;
    } else { // Create
        oflags = O_RDWR | O_CREAT | O_TRUNC;
        prot  |= PROT_WRITE;
    }

    fd_ = ::open(path.c_str(), oflags, 0644);
    if (fd_ < 0) throw_errno(("BinaryStore::open " + path.string()).c_str());

    if (mode == StoreMode::Create) {
        // Write an initial file with capacity for kInitialCapacity records
        const std::size_t init_size =
            page_align(file_size_for<T>(kInitialCapacity));
        if (::ftruncate(fd_, static_cast<off_t>(init_size)) < 0) {
            ::close(fd_); fd_ = -1;
            throw_errno("BinaryStore::ftruncate");
        }
        map_len_ = init_size;
    } else {
        struct stat st{};
        if (::fstat(fd_, &st) < 0) {
            ::close(fd_); fd_ = -1;
            throw_errno("BinaryStore::fstat");
        }
        map_len_ = page_align(static_cast<std::size_t>(st.st_size));
        if (map_len_ == 0) map_len_ = page_align(sizeof(StoreHeader));
    }

    map_ptr_ = ::mmap(nullptr, map_len_, prot, mflags, fd_, 0);
    if (map_ptr_ == MAP_FAILED) {
        ::close(fd_); fd_ = -1; map_ptr_ = nullptr;
        throw_errno("BinaryStore::mmap");
    }

    // Advise sequential read pattern for replay workloads
    ::madvise(map_ptr_, map_len_, MADV_SEQUENTIAL);

    header_ = static_cast<StoreHeader*>(map_ptr_);

    if (mode == StoreMode::Create) {
        header_->magic        = kStoreMagic;
        header_->version      = kStoreVersion;
        header_->record_size  = sizeof(T);
        header_->record_count = 0;
    } else {
        if (header_->magic != kStoreMagic)
            throw std::runtime_error("BinaryStore: bad magic – corrupt or wrong file");
        if (header_->record_size != sizeof(T))
            throw std::runtime_error("BinaryStore: record size mismatch");
    }
}

// ── close_store ───────────────────────────────────────────────────────────────
template<MarketRecord T>
void BinaryStore<T>::close_store() noexcept {
    if (map_ptr_ && map_ptr_ != MAP_FAILED) {
        if (mode_ != StoreMode::ReadOnly) {
            ::msync(map_ptr_, map_len_, MS_SYNC);
        }
        ::munmap(map_ptr_, map_len_);
        map_ptr_ = nullptr;
        map_len_ = 0;
        header_  = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

// ── remap ────────────────────────────────────────────────────────────────────
template<MarketRecord T>
void BinaryStore<T>::remap(std::size_t new_file_size) {
    const std::size_t aligned = page_align(new_file_size);
    if (::ftruncate(fd_, static_cast<off_t>(aligned)) < 0)
        throw_errno("BinaryStore::remap ftruncate");

#ifdef MREMAP_MAYMOVE
    void* new_ptr = ::mremap(map_ptr_, map_len_, aligned, MREMAP_MAYMOVE);
    if (new_ptr == MAP_FAILED) throw_errno("BinaryStore::mremap");
    map_ptr_ = new_ptr;
#else
    ::munmap(map_ptr_, map_len_);
    map_ptr_ = ::mmap(nullptr, aligned, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (map_ptr_ == MAP_FAILED) {
        map_ptr_ = nullptr;
        throw_errno("BinaryStore::mmap (remap)");
    }
#endif
    map_len_ = aligned;
    header_  = static_cast<StoreHeader*>(map_ptr_);
}

// ── append ───────────────────────────────────────────────────────────────────
template<MarketRecord T>
void BinaryStore<T>::append(const T& record) {
    if (mode_ == StoreMode::ReadOnly)
        throw std::logic_error("BinaryStore::append on read-only store");

    const std::size_t needed =
        sizeof(StoreHeader) + (header_->record_count + 1) * sizeof(T);
    if (needed > map_len_) {
        // Double capacity
        remap(std::max(needed, map_len_ * 2));
    }

    std::memcpy(data_ptr() + header_->record_count, &record, sizeof(T));
    ++header_->record_count;
}

// ── flush ────────────────────────────────────────────────────────────────────
template<MarketRecord T>
void BinaryStore<T>::flush() noexcept {
    if (map_ptr_ && mode_ != StoreMode::ReadOnly)
        ::msync(map_ptr_, map_len_, MS_ASYNC);
}

// ── lower_bound ───────────────────────────────────────────────────────────────
template<MarketRecord T>
auto BinaryStore<T>::lower_bound(Timestamp ts) const noexcept -> const_iterator {
    // Binary search over recv_ts field
    auto first = begin();
    auto last  = end();
    auto count = static_cast<std::ptrdiff_t>(size());

    while (count > 0) {
        auto half = count / 2;
        auto mid  = first + half;
        if (mid->recv_ts < ts) {
            first  = mid + 1;
            count -= half + 1;
        } else {
            count = half;
        }
    }
    return first;
}

// ── last_at_or_before ────────────────────────────────────────────────────────
template<MarketRecord T>
std::optional<T> BinaryStore<T>::last_at_or_before(Timestamp ts) const noexcept {
    auto it = lower_bound(ts + 1); // first record > ts
    if (it == begin()) return std::nullopt;
    --it;
    return *it;
}

// ── Explicit instantiations ───────────────────────────────────────────────────
template class BinaryStore<Tick>;
template class BinaryStore<Bar>;

} // namespace md
