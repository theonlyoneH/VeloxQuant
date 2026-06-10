// networking/binary_protocol.cpp  ── Wire format encoder/decoder and CRC32

#include "networking/binary_protocol.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace net {

// ── CRC32 (software, no dependency on zlib) ───────────────────────────────────
namespace {

constexpr std::array<uint32_t, 256> make_crc_table() noexcept {
    std::array<uint32_t, 256> tbl{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        tbl[i] = c;
    }
    return tbl;
}

constexpr auto kCrcTable = make_crc_table();

} // anon namespace

uint32_t crc32(std::span<const std::byte> data) noexcept {
    uint32_t crc = 0xFFFF'FFFFu;
    for (auto b : data) {
        const uint8_t idx = (crc ^ static_cast<uint8_t>(b)) & 0xFFu;
        crc = (crc >> 8) ^ kCrcTable[idx];
    }
    return crc ^ 0xFFFF'FFFFu;
}

// ── Little-endian write helpers ───────────────────────────────────────────────
namespace {

void write_u8(std::byte* p, uint8_t v) noexcept {
    *p = static_cast<std::byte>(v);
}
void write_u16le(std::byte* p, uint16_t v) noexcept {
    p[0] = static_cast<std::byte>(v & 0xFF);
    p[1] = static_cast<std::byte>((v >> 8) & 0xFF);
}
void write_u32le(std::byte* p, uint32_t v) noexcept {
    p[0] = static_cast<std::byte>(v & 0xFF);
    p[1] = static_cast<std::byte>((v >> 8) & 0xFF);
    p[2] = static_cast<std::byte>((v >> 16) & 0xFF);
    p[3] = static_cast<std::byte>((v >> 24) & 0xFF);
}

uint8_t  read_u8  (const std::byte* p) noexcept { return static_cast<uint8_t>(*p); }
uint16_t read_u16le(const std::byte* p) noexcept {
    return static_cast<uint16_t>(static_cast<uint8_t>(p[0]))
         | (static_cast<uint16_t>(static_cast<uint8_t>(p[1])) << 8);
}
uint32_t read_u32le(const std::byte* p) noexcept {
    return static_cast<uint32_t>(static_cast<uint8_t>(p[0]))
         | (static_cast<uint32_t>(static_cast<uint8_t>(p[1])) << 8)
         | (static_cast<uint32_t>(static_cast<uint8_t>(p[2])) << 16)
         | (static_cast<uint32_t>(static_cast<uint8_t>(p[3])) << 24);
}

/// Write a full wire frame around `payload`.
/// buf must be at least kFrameOverhead + payload.size_bytes() bytes.
std::size_t write_frame(MessageType type,
                        std::span<const std::byte> payload,
                        std::span<std::byte>       buf) noexcept {
    const std::size_t total = kFrameOverhead + payload.size();
    if (buf.size() < total) return 0;

    std::byte* p = buf.data();
    write_u32le(p, kWireMagic);           p += 4;
    write_u8(p, static_cast<uint8_t>(type)); ++p;
    write_u16le(p, static_cast<uint16_t>(payload.size())); p += 2;
    std::memcpy(p, payload.data(), payload.size());

    // CRC over [magic..payload] = kHdrSize + payload.size() bytes
    const uint32_t checksum = crc32({buf.data(), kHdrSize + payload.size()});
    p += payload.size();
    write_u32le(p, checksum);

    return total;
}

template<typename T>
std::size_t encode_trivial(MessageType type, const T& msg,
                           std::span<std::byte> buf) noexcept {
    std::array<std::byte, sizeof(T)> payload{};
    std::memcpy(payload.data(), &msg, sizeof(T));
    return write_frame(type, payload, buf);
}

} // anon namespace

// ── Encoders ─────────────────────────────────────────────────────────────────
std::size_t encode(const Tick& msg, std::span<std::byte> buf) noexcept {
    return encode_trivial(MessageType::Tick, msg, buf);
}
std::size_t encode(const Bar& msg, std::span<std::byte> buf) noexcept {
    return encode_trivial(MessageType::Bar, msg, buf);
}
std::size_t encode(const HeartbeatMsg& msg, std::span<std::byte> buf) noexcept {
    return encode_trivial(MessageType::Heartbeat, msg, buf);
}
std::size_t encode(const SubscribeRequest& msg, std::span<std::byte> buf) noexcept {
    return encode_trivial(MessageType::Subscribe, msg, buf);
}
std::size_t encode(const AckMessage& msg, std::span<std::byte> buf) noexcept {
    return encode_trivial(MessageType::Ack, msg, buf);
}

// ── BinaryParser ─────────────────────────────────────────────────────────────
BinaryParser::BinaryParser(MessageCallback cb) noexcept
    : callback_(std::move(cb))
{
    buf_.reserve(kMaxFrameSize * 2);
}

void BinaryParser::reset() noexcept {
    state_ = State::SeekMagic;
    buf_.clear();
    payload_len_ = 0;
    msg_type_    = MessageType::Unknown;
}

void BinaryParser::push(std::span<const std::byte> data) {
    for (auto b : data) process_byte(b);
}

bool BinaryParser::check_magic() const noexcept {
    if (buf_.size() < 4) return false;
    return read_u32le(buf_.data()) == kWireMagic;
}

void BinaryParser::process_byte(std::byte b) {
    switch (state_) {
    // ── SEEK MAGIC ────────────────────────────────────────────────────────────
    case State::SeekMagic: {
        buf_.push_back(b);
        if (buf_.size() < 4) return;
        if (check_magic()) {
            state_ = State::ReadHeader;
        } else {
            // Slide window: discard first byte
            buf_.erase(buf_.begin());
        }
        break;
    }

    // ── READ HEADER (need 3 more bytes after the 4-byte magic) ───────────────
    case State::ReadHeader: {
        buf_.push_back(b);
        if (buf_.size() < kHdrSize) return;

        msg_type_    = static_cast<MessageType>(read_u8(buf_.data() + 4));
        payload_len_ = read_u16le(buf_.data() + 5);

        if (payload_len_ > kMaxPayload) {
            ++frames_rejected_;
            if (callback_)
                callback_({false, {}, ParseError::PayloadTooLarge});
            reset();
            return;
        }

        state_ = (payload_len_ > 0) ? State::ReadPayload : State::ReadTrailer;
        break;
    }

    // ── READ PAYLOAD ──────────────────────────────────────────────────────────
    case State::ReadPayload: {
        buf_.push_back(b);
        if (buf_.size() < kHdrSize + payload_len_) return;
        state_ = State::ReadTrailer;
        break;
    }

    // ── READ TRAILER (4-byte CRC) ─────────────────────────────────────────────
    case State::ReadTrailer: {
        buf_.push_back(b);
        if (buf_.size() < kHdrSize + payload_len_ + kTrlSize) return;
        dispatch_frame();
        break;
    }
    }
}

void BinaryParser::dispatch_frame() {
    // Verify CRC
    const std::size_t crc_offset = kHdrSize + payload_len_;
    const uint32_t expected_crc  = crc32({buf_.data(), crc_offset});
    const uint32_t wire_crc      = read_u32le(buf_.data() + crc_offset);

    if (wire_crc != expected_crc) {
        ++frames_rejected_;
        if (callback_)
            callback_({false, {}, ParseError::BadCRC});
        reset();
        return;
    }

    const std::byte* payload_ptr = buf_.data() + kHdrSize;

    auto try_decode = [&]<typename T>(T& out) -> bool {
        if (payload_len_ < sizeof(T)) return false;
        std::memcpy(&out, payload_ptr, sizeof(T));
        return true;
    };

    ParseResult result;
    result.ok = true;

    switch (msg_type_) {
    case MessageType::Tick: {
        Tick t{};
        if (!try_decode(t)) { result.ok = false; result.error = ParseError::Truncated; break; }
        result.message = t;
        break;
    }
    case MessageType::Bar: {
        Bar b{};
        if (!try_decode(b)) { result.ok = false; result.error = ParseError::Truncated; break; }
        result.message = b;
        break;
    }
    case MessageType::Heartbeat: {
        HeartbeatMsg h{};
        if (payload_len_ >= sizeof(h)) std::memcpy(&h, payload_ptr, sizeof(h));
        result.message = h;
        break;
    }
    case MessageType::Subscribe: {
        SubscribeRequest s{};
        if (!try_decode(s)) { result.ok = false; result.error = ParseError::Truncated; break; }
        result.message = s;
        break;
    }
    case MessageType::Ack: {
        AckMessage a{};
        if (!try_decode(a)) { result.ok = false; result.error = ParseError::Truncated; break; }
        result.message = a;
        break;
    }
    default:
        result.ok    = false;
        result.error = ParseError::UnknownType;
        break;
    }

    if (result.ok) ++frames_decoded_;
    else           ++frames_rejected_;

    if (callback_) callback_(result);
    reset();
}

} // namespace net
