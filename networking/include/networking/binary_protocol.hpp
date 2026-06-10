#pragma once
// networking/binary_protocol.hpp  ── Wire format and streaming binary parser
//
// Wire frame layout (all multi-byte fields are little-endian):
//
//  ┌─────────────────────────────────────────────────────────┐
//  │  4 bytes │  1 byte   │  2 bytes  │  N bytes  │ 4 bytes  │
//  │  Magic   │ MsgType   │  Length   │  Payload  │  CRC32   │
//  └─────────────────────────────────────────────────────────┘
//
//  Magic   = 0xFEED'0001
//  Length  = sizeof(payload) only (not including header/trailer)
//  CRC32   = CRC32 over [Magic .. Payload] (everything except the CRC field)
//
// Supported message types:
//   TICK       (0x01) – md::Tick  struct verbatim
//   BAR        (0x02) – md::Bar   struct verbatim
//   HEARTBEAT  (0x03) – empty payload
//   SUBSCRIBE  (0x10) – SubscribeRequest: symbol_id + flags
//   ACK        (0x11) – AckMessage: status code + sequence
//
// The BinaryParser is a zero-copy, zero-allocation streaming state machine.
// Feed raw bytes via push(); it fires a callback when a complete message
// has been validated and decoded.

#include "market_data/types.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace net {

using md::Tick;
using md::Bar;
using md::SymbolId;

// ── Constants ─────────────────────────────────────────────────────────────────
inline constexpr uint32_t kWireMagic   = 0xFEED0001u;
inline constexpr std::size_t kHdrSize  = 7;   ///< magic(4) + type(1) + len(2)
inline constexpr std::size_t kTrlSize  = 4;   ///< CRC32(4)
inline constexpr std::size_t kFrameOverhead = kHdrSize + kTrlSize;
inline constexpr std::size_t kMaxPayload    = 65535;

// ── MessageType ───────────────────────────────────────────────────────────────
enum class MessageType : uint8_t {
    Tick      = 0x01,
    Bar       = 0x02,
    Heartbeat = 0x03,
    Subscribe = 0x10,
    Ack       = 0x11,
    Unknown   = 0xFF,
};

// ── Payload types ─────────────────────────────────────────────────────────────
struct HeartbeatMsg {
    md::Timestamp server_ts; ///< Server-side timestamp (ns)
};

struct SubscribeRequest {
    SymbolId symbol_id;
    uint8_t  flags;     ///< bit 0 = subscribe, bit 1 = tick, bit 2 = bar
    uint8_t  _pad[3]{};
};
static_assert(std::is_trivially_copyable_v<SubscribeRequest>);

struct AckMessage {
    uint8_t  status;    ///< 0 = OK, 1 = error
    uint8_t  _pad[3]{};
    md::SequenceNo sequence;
};
static_assert(std::is_trivially_copyable_v<AckMessage>);

// ── Decoded message variant ───────────────────────────────────────────────────
using DecodedMessage = std::variant<
    Tick,
    Bar,
    HeartbeatMsg,
    SubscribeRequest,
    AckMessage
>;

// ── ParseResult ───────────────────────────────────────────────────────────────
enum class ParseError {
    BadMagic,
    BadCRC,
    UnknownType,
    PayloadTooLarge,
    Truncated,
};

struct ParseResult {
    bool            ok;
    DecodedMessage  message;
    ParseError      error;  ///< valid only when ok == false
};

// ── Encoder ───────────────────────────────────────────────────────────────────
/// Encodes a message into a caller-supplied buffer.
/// Returns the number of bytes written, or 0 on failure.
std::size_t encode(const Tick&            msg, std::span<std::byte> buf) noexcept;
std::size_t encode(const Bar&             msg, std::span<std::byte> buf) noexcept;
std::size_t encode(const HeartbeatMsg&    msg, std::span<std::byte> buf) noexcept;
std::size_t encode(const SubscribeRequest&msg, std::span<std::byte> buf) noexcept;
std::size_t encode(const AckMessage&      msg, std::span<std::byte> buf) noexcept;

/// Minimum buffer size needed to encode any message
inline constexpr std::size_t kMaxFrameSize =
    kFrameOverhead + std::max({sizeof(Tick), sizeof(Bar),
                               sizeof(HeartbeatMsg), sizeof(SubscribeRequest),
                               sizeof(AckMessage)});

// ── CRC32 ─────────────────────────────────────────────────────────────────────
uint32_t crc32(std::span<const std::byte> data) noexcept;

// ── BinaryParser (streaming state machine) ───────────────────────────────────
class BinaryParser {
public:
    using MessageCallback = std::function<void(ParseResult)>;

    explicit BinaryParser(MessageCallback cb) noexcept;

    // Non-copyable, movable
    BinaryParser(const BinaryParser&)            = delete;
    BinaryParser& operator=(const BinaryParser&) = delete;
    BinaryParser(BinaryParser&&)                 = default;
    BinaryParser& operator=(BinaryParser&&)      = default;

    /// Feed raw bytes into the parser.
    /// The callback is invoked for each complete, validated frame.
    void push(std::span<const std::byte> data);

    /// Reset parser state (e.g. after a connection reset).
    void reset() noexcept;

    /// Total frames decoded successfully.
    [[nodiscard]] uint64_t frames_decoded() const noexcept { return frames_decoded_; }
    /// Total frames rejected (bad magic / CRC).
    [[nodiscard]] uint64_t frames_rejected() const noexcept { return frames_rejected_; }

private:
    // ── State machine ─────────────────────────────────────────────────────────
    enum class State {
        SeekMagic,      ///< Scanning for 0xFEED0001
        ReadHeader,     ///< Collecting 7-byte header
        ReadPayload,    ///< Collecting payload bytes
        ReadTrailer,    ///< Collecting 4-byte CRC
    };

    State            state_      {State::SeekMagic};
    std::vector<std::byte> buf_; ///< Internal ring accumulator
    std::size_t      payload_len_{0};
    MessageType      msg_type_   {MessageType::Unknown};
    uint64_t         frames_decoded_{0};
    uint64_t         frames_rejected_{0};
    MessageCallback  callback_;

    void process_byte(std::byte b);
    void dispatch_frame();
    bool check_magic() const noexcept;
};

} // namespace net
