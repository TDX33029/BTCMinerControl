#include "protocol.h"
#include "../mine/job.h"
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Helper: write big-endian integers
// ---------------------------------------------------------------------------
static void write_u32(uint8_t* buf, uint32_t val) {
    buf[0] = uint8_t(val >> 24);
    buf[1] = uint8_t(val >> 16);
    buf[2] = uint8_t(val >> 8);
    buf[3] = uint8_t(val);
}

static void write_u16(uint8_t* buf, uint16_t val) {
    buf[0] = uint8_t(val >> 8);
    buf[1] = uint8_t(val);
}

static void write_u64(uint8_t* buf, uint64_t val) {
    for (int i = 7; i >= 0; i--) {
        buf[7 - i] = uint8_t(val >> (i * 8));
    }
}

static uint32_t read_u32(const uint8_t* buf) {
    return (uint32_t(buf[0]) << 24) | (uint32_t(buf[1]) << 16)
         | (uint32_t(buf[2]) << 8)  |  uint32_t(buf[3]);
}

static uint64_t read_u64(const uint8_t* buf) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v = (v << 8) | buf[i];
    }
    return v;
}

// ---------------------------------------------------------------------------
// Build a complete framed message
// ---------------------------------------------------------------------------
static std::vector<uint8_t> frame_message(MsgType type, const uint8_t* payload, size_t payload_len) {
    std::vector<uint8_t> framed(5 + payload_len);
    write_u32(framed.data(), uint32_t(payload_len + 1)); // length includes type byte
    framed[4] = uint8_t(type);
    if (payload_len > 0) {
        memcpy(framed.data() + 5, payload, payload_len);
    }
    return framed;
}

// ---------------------------------------------------------------------------
// Encode job → wire format
// ---------------------------------------------------------------------------
std::vector<uint8_t> encode_job(const MinerJob& job) {
    // Calculate payload size
    size_t midstate_total = job.midstates.size() * 32;
    // job_id(1) + num_midstates(1) + midstates + version(4) + prev_hash(32) + merkle_root(32) + ntime(4) + nbits(4) + starting_nonce(4)
    size_t payload_len = 1 + 1 + midstate_total + 4 + 32 + 32 + 4 + 4 + 4;

    std::vector<uint8_t> payload(payload_len);
    size_t off = 0;

    payload[off++] = job.job_id;
    payload[off++] = job.num_midstates;

    for (const auto& ms : job.midstates) {
        memcpy(&payload[off], ms.data(), 32);
        off += 32;
    }

    write_u32(&payload[off], job.version); off += 4;
    memcpy(&payload[off], job.prev_block_hash, 32); off += 32;
    memcpy(&payload[off], job.merkle_root, 32); off += 32;
    write_u32(&payload[off], job.ntime); off += 4;
    write_u32(&payload[off], job.nbits); off += 4;
    write_u32(&payload[off], job.starting_nonce); off += 4;

    return frame_message(MsgType::Job, payload.data(), payload_len);
}

// ---------------------------------------------------------------------------
// Decode nonce result
// ---------------------------------------------------------------------------
bool decode_nonce_result(const uint8_t* data, size_t len, NonceResult& out) {
    if (len < 18) return false;

    out.job_id   = data[0];
    out.asic_nr  = data[1];
    out.nonce    = read_u32(data + 2);
    out.rolled_version = read_u32(data + 6);
    out.timestamp_us   = read_u64(data + 10);

    return true;
}

// ---------------------------------------------------------------------------
// Decode board hello
// ---------------------------------------------------------------------------
bool decode_board_hello(const uint8_t* data, size_t len, BoardHello& out) {
    if (len < 12) return false;

    out.board_id   = read_u64(data);
    out.asic_count = data[8];
    out.fw_version = (uint16_t(data[9]) << 8) | data[10];
    out.status     = data[11];

    return true;
}

// ---------------------------------------------------------------------------
// Decode ASIC register
// ---------------------------------------------------------------------------
bool decode_asic_register(const uint8_t* data, size_t len, AsicRegister& out) {
    if (len < 6) return false;

    out.asic_nr       = data[0];
    out.register_type = data[1];
    out.value         = read_u32(data + 2);

    return true;
}

// ---------------------------------------------------------------------------
// Encode set params
// ---------------------------------------------------------------------------
std::vector<uint8_t> encode_set_params(uint16_t freq_mhz, uint16_t voltage_mv) {
    uint8_t payload[4];
    write_u16(payload, freq_mhz);
    write_u16(payload + 2, voltage_mv);
    return frame_message(MsgType::SetParams, payload, 4);
}

// ---------------------------------------------------------------------------
// Encode ACK
// ---------------------------------------------------------------------------
std::vector<uint8_t> encode_ack(uint8_t ack_type) {
    return frame_message(MsgType::Ack, &ack_type, 1);
}

// ---------------------------------------------------------------------------
// Encode error
// ---------------------------------------------------------------------------
std::vector<uint8_t> encode_error(uint8_t code, const std::string& msg) {
    std::vector<uint8_t> payload(1 + msg.length());
    payload[0] = code;
    memcpy(payload.data() + 1, msg.data(), msg.length());
    return frame_message(MsgType::Error, payload.data(), payload.size());
}

// ---------------------------------------------------------------------------
// Receive a complete framed message
// ---------------------------------------------------------------------------
std::vector<uint8_t> recv_message(SOCKET sock, int timeout_ms) {
    // Set timeout
    if (timeout_ms > 0) {
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
    }

    // Read 4-byte length
    uint8_t len_buf[4];
    int total = 0;
    while (total < 4) {
        int n = recv(sock, (char*)(len_buf + total), 4 - total, 0);
        if (n <= 0) return {};
        total += n;
    }

    uint32_t msg_len = read_u32(len_buf);
    if (msg_len > 1024 * 1024) return {}; // sanity check: max 1MB

    // Read the rest (type byte + payload)
    std::vector<uint8_t> data(msg_len);
    total = 0;
    while (total < (int)msg_len) {
        int n = recv(sock, (char*)(data.data() + total), int(msg_len) - total, 0);
        if (n <= 0) return {};
        total += n;
    }

    return data; // [type_byte, payload...]
}
