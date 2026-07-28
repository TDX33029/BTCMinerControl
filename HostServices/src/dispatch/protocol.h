#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <winsock2.h>

// ---------------------------------------------------------------------------
// Binary protocol for PC ↔ STM32 board communication over TCP.
//
// Frame format (big-endian):
//   [Length:4B] [Type:1B] [Payload:N bytes]
//
// Message types:
//   0x01 PC→Board: Job
//   0x02 Board→PC: Nonce result
//   0x03 Board→PC: ASIC register
//   0x04 Board→PC: Register/Heartbeat
//   0x05 PC→Board: Set frequency/voltage
//   0x06 PC→Board: ACK
//   0xFF Both: NACK/Error
// ---------------------------------------------------------------------------

// Forward declare
struct MinerJob;
struct NonceResult;

// Message type constants
enum class MsgType : uint8_t {
    Job          = 0x01,
    NonceResult  = 0x02,
    AsicRegister = 0x03,
    BoardHello   = 0x04,
    SetParams    = 0x05,
    Ack          = 0x06,
    Error        = 0xFF,
};

// Board registration info
struct BoardInfo {
    uint64_t    board_id;
    uint8_t     asic_count;
    uint16_t    firmware_version;
    uint8_t     status;          // 0=ok, 1=warning, 2=error
    SOCKET      socket;
    std::string ip_addr;
    uint64_t    last_heartbeat;
    uint64_t    shares_accepted;
    uint64_t    shares_rejected;
    double      current_hashrate; // GH/s
};

// Board register/heartbeat from board
struct BoardHello {
    uint64_t board_id;
    uint8_t  asic_count;
    uint16_t fw_version;
    uint8_t  status;
};

// ASIC register read from board
struct AsicRegister {
    uint8_t asic_nr;
    uint8_t register_type;
    uint32_t value;
};

// Encode a job into the binary wire format.
// Returns the complete framed message ready to send.
std::vector<uint8_t> encode_job(const MinerJob& job);

// Decode a nonce result from the binary wire format.
// Returns true on success.
bool decode_nonce_result(const uint8_t* data, size_t len, NonceResult& out);

// Decode a board hello message.
bool decode_board_hello(const uint8_t* data, size_t len, BoardHello& out);

// Decode an ASIC register message.
bool decode_asic_register(const uint8_t* data, size_t len, AsicRegister& out);

// Encode a frequency/voltage setting command.
std::vector<uint8_t> encode_set_params(uint16_t freq_mhz, uint16_t voltage_mv);

// Encode an ACK message.
std::vector<uint8_t> encode_ack(uint8_t ack_type);

// Encode an error message.
std::vector<uint8_t> encode_error(uint8_t code, const std::string& msg);

enum class ReceiveResult {
    Message,
    Timeout,
    Closed,
    SocketError,
    ProtocolError,
};

// Stateful TCP frame reader. Partial length fields and payloads remain in the
// buffer across receive timeouts, and coalesced frames are returned one by one.
class MessageReader {
public:
    ReceiveResult receive(SOCKET sock, std::vector<uint8_t>& message,
                          int timeout_ms = 500);
    void clear() { m_buffer.clear(); }

private:
    static constexpr uint32_t kMaxMessageLength = 4096;
    std::vector<uint8_t> m_buffer;
};
