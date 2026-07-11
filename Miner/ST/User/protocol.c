/**
 * PC↔STM32 二进制协议编解码
 *
 * 与 BTCMinerControl / HostServices/src/dispatch/protocol.cpp 格式严格一致。
 *
 * 帧格式 (大端):
 *   [Length:4B] [Type:1B] [Payload:N bytes]
 *   Length = N + 1  (包含 Type 字节)
 *
 * 作业载荷 (Type=0x01, 来自 PC):
 *   job_id(1) + num_midstates(1) + midstates(N*32) + version(4) +
 *   prev_block_hash(32) + merkle_root(32) + ntime(4) + nbits(4) + starting_nonce(4)
 *
 * Nonce结果载荷 (Type=0x02, 发往 PC):
 *   job_id(1) + asic_nr(1) + nonce(4) + rolled_version(4) + timestamp_us(8)
 *
 * BoardHello载荷 (Type=0x04, 发往 PC):
 *   board_id(8) + asic_count(1) + fw_version(2) + status(1)
 */

#include "protocol.h"
#include <string.h>
#include <stdio.h>

/* ===== 私有: 大端读写 ===== */

static void write_u32(uint8_t *buf, uint32_t val) {
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)(val >> 16);
    buf[2] = (uint8_t)(val >> 8);
    buf[3] = (uint8_t)(val);
}

static void write_u16(uint8_t *buf, uint16_t val) {
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val);
}

static void write_u64(uint8_t *buf, uint64_t val) {
    for (int i = 7; i >= 0; i--) {
        buf[7 - i] = (uint8_t)(val >> (i * 8));
    }
}

static uint32_t read_u32(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3];
}

/* ===== 帧封装 ===== */

/**
 * 构建一个完整的二进制帧。
 * @param type    消息类型
 * @param payload 载荷数据
 * @param payload_len 载荷长度
 * @param buf     输出缓冲区 (必须至少 5 + payload_len)
 * @return 帧总长度
 */
static uint16_t build_frame(uint8_t type, const uint8_t *payload,
                             uint16_t payload_len, uint8_t *buf) {
    write_u32(buf, payload_len + 1); /* Length = payload + type byte */
    buf[4] = type;
    if (payload_len > 0) {
        memcpy(buf + 5, payload, payload_len);
    }
    return payload_len + 5;
}

/* ===== Job 解码 (Type=0x01) ===== */

int protocol_decode_job(const uint8_t *data, uint16_t len, protocol_job_t *job) {
    /* 最小长度: job_id(1) + num_midstates(1) + 1*mids(32) + version(4) +
                 prev(32) + merkle(32) + ntime(4) + nbits(4) + nonce(4) = 114 */
    if (len < 114) return 0;

    memset(job, 0, sizeof(protocol_job_t));

    uint16_t off = 0;
    job->job_id        = data[off];  off += 1;
    job->num_midstates = data[off];  off += 1;

    if (job->num_midstates > 4) job->num_midstates = 4;

    for (uint8_t i = 0; i < job->num_midstates; i++) {
        memcpy(job->midstates[i], data + off, 32);
        off += 32;
    }

    /* 大端读取 version (但实际 ASIC 需要小端，保持原始大端读取再交换) */
    job->version = read_u32(data + off); off += 4;

    memcpy(job->prev_block_hash, data + off, 32); off += 32;
    memcpy(job->merkle_root,      data + off, 32); off += 32;

    job->ntime          = read_u32(data + off); off += 4;
    job->nbits          = read_u32(data + off); off += 4;
    job->starting_nonce = read_u32(data + off); off += 4;

    return 1;
}

/* ===== SetParams 解码 (Type=0x05) ===== */

int protocol_decode_setparams(const uint8_t *data, uint16_t len,
                              protocol_setparams_t *params) {
    if (len < 4) return 0;
    params->freq_mhz   = ((uint16_t)data[0] << 8) | data[1];
    params->voltage_mv = ((uint16_t)data[2] << 8) | data[3];
    return 1;
}

/* ===== BoardHello 编码 (Type=0x04) ===== */

uint16_t protocol_encode_hello(const protocol_hello_t *hello, uint8_t *buf) {
    uint8_t payload[12];
    write_u64(payload,     hello->board_id);
    payload[8] = hello->asic_count;
    write_u16(payload + 9, hello->fw_version);
    payload[11] = hello->status;
    return build_frame(MSG_BOARD_HELLO, payload, 12, buf);
}

/* ===== NonceResult 编码 (Type=0x02) ===== */

uint16_t protocol_encode_nonce(const bm1366_result_t *result,
                               uint64_t board_id __attribute__((unused)), uint8_t *buf) {
    /* 载荷 = job_id(1) + asic_nr(1) + nonce(4) + rolled_version(4) + timestamp(8) */
    /* 注意: 与 BTCMinerControl protocol.cpp decode_nonce_result 格式一致
     *       board_id 由 PC 端的 recvLoop 通过 socket 跟踪, 不在帧内传输 */
    uint8_t payload[18];

    payload[0] = result->job_id;
    payload[1] = result->asic_nr;
    write_u32(payload + 2,  result->nonce);
    write_u32(payload + 6,  result->rolled_version);
    /* timestamp: 用 SysTick 毫秒 (精度足够) */
    extern volatile uint32_t g_ms;
    write_u64(payload + 10, (uint64_t)g_ms * 1000ULL); /* ms → us */

    return build_frame(MSG_NONCE_RESULT, payload, 18, buf);
}

/* ===== AsicRegister 编码 (Type=0x03) ===== */

uint16_t protocol_encode_asic_reg(uint8_t asic_nr, uint8_t reg_type,
                                  uint32_t value, uint8_t *buf) {
    uint8_t payload[6];
    payload[0] = asic_nr;
    payload[1] = reg_type;
    write_u32(payload + 2, value);
    return build_frame(MSG_ASIC_REGISTER, payload, 6, buf);
}

/* ===== Ack 编码 (Type=0x06) ===== */

uint16_t protocol_encode_ack(uint8_t ack_type, uint8_t *buf) {
    return build_frame(MSG_ACK, &ack_type, 1, buf);
}

/* ===== Error 编码 (Type=0xFF) ===== */

uint16_t protocol_encode_error(uint8_t code, const char *msg, uint8_t *buf) {
    uint16_t msg_len = (uint16_t)strlen(msg);
    if (msg_len > 120) msg_len = 120; /* 限制最大长度 */

    uint8_t payload[128];
    payload[0] = code;
    memcpy(payload + 1, msg, msg_len);
    return build_frame(MSG_ERROR, payload, msg_len + 1, buf);
}

/* ===== 帧同步解析 ===== */

/**
 * 检查缓冲区中是否包含一个完整帧。
 * 扫描前导长度字段，验证 payload 是否已全部到达。
 *
 * @param buf          接收缓冲区
 * @param buf_len      缓冲区中的数据量
 * @param frame_len    [out] 帧总长度 (5 + payload)
 * @param payload_len  [out] 载荷长度 (不含 type)
 * @return 0=不完整/无效, 非0=消息类型
 */
uint8_t protocol_peek_frame(const uint8_t *buf, uint16_t buf_len,
                            uint16_t *frame_len, uint16_t *payload_len) {
    if (buf_len < 5) return 0;

    uint32_t total = read_u32(buf);
    if (total > 2048 || total < 1) return 0; /* 长度校验 */
    if (buf_len < total + 4) return 0;        /* 等待更多数据 */

    *frame_len   = (uint16_t)(total + 4);
    *payload_len = (uint16_t)(total - 1);

    return buf[4]; /* 返回类型 */
}
