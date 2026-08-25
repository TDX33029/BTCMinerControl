#include "protocol.h"
#include "../platform/platform.h"
#include "../mine/job.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>

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

static uint16_t read_u16(const uint8_t* buf) {
    return (uint16_t(buf[0]) << 8) | uint16_t(buf[1]);
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
    if (job.midstates.empty() || job.midstates.size() > 4 ||
        job.num_midstates != job.midstates.size()) {
        throw std::invalid_argument("job has an invalid midstate count");
    }
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
    out.target_frequency_mhz = 0;
    out.actual_frequency_mhz = 0;
    if (len >= 16) {
        out.target_frequency_mhz = read_u16(data + 12);
        out.actual_frequency_mhz = read_u16(data + 14);
    }

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

bool decode_hashrate_sample(const uint8_t* data, size_t len, uint32_t& mhs) {
    if (len < 4) return false;
    mhs = read_u32(data);
    return true;
}

bool decode_frequency_status(const uint8_t* data, size_t len,
                             uint16_t& target_mhz, uint16_t& actual_mhz) {
    if (len < 4) return false;
    target_mhz = read_u16(data);
    actual_mhz = read_u16(data + 2);
    return true;
}

bool decode_board_telemetry(const uint8_t* data, size_t len,
                            BoardTelemetry& out) {
    if (data == nullptr || len < 20) return false;

    out.flags = data[0];
    out.tps_address = data[1];
    out.tmp1075_address = data[2];
    out.power_enabled = data[3];
    out.vout_mv = read_u16(data + 4);
    out.iout_ma = read_u32(data + 6);
    out.power_mw = read_u32(data + 10);
    out.tmp1075_temperature_centi_c =
        static_cast<int16_t>(read_u16(data + 14));
    out.tps_temperature_centi_c =
        static_cast<int16_t>(read_u16(data + 16));
    out.tps_status_word = read_u16(data + 18);
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

std::vector<uint8_t> encode_set_version_mask(uint32_t version_mask) {
    uint8_t payload[4];
    write_u32(payload, version_mask);
    return frame_message(MsgType::SetVersionMask, payload, sizeof(payload));
}

std::vector<uint8_t> encode_set_frequency(uint16_t frequency_mhz) {
    uint8_t payload[2];
    write_u16(payload, frequency_mhz);
    return frame_message(MsgType::SetFrequency, payload, sizeof(payload));
}

std::vector<uint8_t> encode_set_power(bool enabled) {
    const uint8_t payload = enabled ? 1U : 0U;
    return frame_message(MsgType::SetPower, &payload, 1);
}

std::vector<uint8_t> encode_latency_probe(uint64_t token) {
    uint8_t payload[8];
    write_u64(payload, token);
    return frame_message(MsgType::LatencyProbe, payload, sizeof(payload));
}

bool decode_latency_probe(const uint8_t* data, size_t len, uint64_t& token) {
    if (data == nullptr || len != 8) return false;
    token = read_u64(data);
    return true;
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

ReceiveResult MessageReader::receive(
    SOCKET sock, std::vector<uint8_t>& message, int timeout_ms) {
    message.clear();

    for (;;) {
        if (m_buffer.size() >= 4) {
            const uint32_t msg_len = read_u32(m_buffer.data());
            if (msg_len == 0 || msg_len > kMaxMessageLength) {
                m_buffer.clear();
                return ReceiveResult::ProtocolError;
            }

            const size_t frame_len = size_t(msg_len) + 4;
            if (m_buffer.size() >= frame_len) {
                message.assign(m_buffer.begin() + 4,
                               m_buffer.begin() + frame_len);
                m_buffer.erase(m_buffer.begin(), m_buffer.begin() + frame_len);
                return ReceiveResult::Message;
            }
        }

        platform::set_recv_timeout(sock, timeout_ms <= 0 ? 1 : timeout_ms);

        uint8_t chunk[2048];
        const int received = recv(sock, reinterpret_cast<char*>(chunk),
                                  static_cast<int>(sizeof(chunk)), 0);
        if (received > 0) {
            m_buffer.insert(m_buffer.end(), chunk, chunk + received);
            if (m_buffer.size() > kMaxMessageLength + 4) {
                m_buffer.clear();
                return ReceiveResult::ProtocolError;
            }
            continue;
        }
        if (received == 0) return ReceiveResult::Closed;

        const int error = WSAGetLastError();
        if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
            return ReceiveResult::Timeout;
        }
        return ReceiveResult::SocketError;
    }
}
