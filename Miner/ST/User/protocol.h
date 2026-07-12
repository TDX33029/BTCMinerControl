#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include "stm32f10x.h"
#include <stdint.h>
#include "bm1366.h"

/* ===== PC↔STM32 二进制协议 =====
 *
 * 帧格式 (大端):
 *   [Length:4B] [Type:1B] [Payload:N bytes]
 *
 * 消息类型 (与 BTCMinerControl protocol.h 一致):
 *   0x01 PC→Board: Job
 *   0x02 Board→PC: Nonce result
 *   0x03 Board→PC: ASIC register
 *   0x04 Board→PC: Register/Heartbeat
 *   0x05 PC→Board: Set frequency/voltage
 *   0x06 PC→Board: ACK
 *   0xFF Both: NACK/Error
 */

typedef enum {
    MSG_JOB           = 0x01,
    MSG_NONCE_RESULT  = 0x02,
    MSG_ASIC_REGISTER = 0x03,
    MSG_BOARD_HELLO   = 0x04,
    MSG_SET_PARAMS    = 0x05,
    MSG_ACK           = 0x06,
    MSG_ERROR         = 0xFF,
} protocol_msg_type_t;

/* ===== 解码后的 Job (来自 PC) ===== */
/* 与 BTCMinerControl MinerJob 对应，但只取我们需要的字段 */
typedef struct {
    uint8_t  job_id;
    uint8_t  num_midstates;
    uint8_t  midstates[4][32];  /* 最多 4 个 midstate */
    uint32_t version;           /* 小端 */
    uint8_t  prev_block_hash[32];
    uint8_t  merkle_root[32];
    uint32_t ntime;
    uint32_t nbits;
    uint32_t starting_nonce;
} protocol_job_t;

/* ===== 解码后的 SetParams (来自 PC) ===== */
typedef struct {
    uint16_t freq_mhz;
    uint16_t voltage_mv;
} protocol_setparams_t;

/* ===== BoardHello 信息 ===== */
typedef struct {
    uint64_t board_id;
    uint8_t  asic_count;
    uint16_t fw_version;
    uint8_t  status;         /* 0=ok, 1=warning, 2=error */
} protocol_hello_t;

/* ===== 公开函数 ===== */

/* Decode a job from the PC wire format (payload after type byte).
 * Returns 1 on success. */
int protocol_decode_job(const uint8_t *data, uint16_t len, protocol_job_t *job);

/* Decode SetParams from PC wire format. Returns 1 on success. */
int protocol_decode_setparams(const uint8_t *data, uint16_t len,
                              protocol_setparams_t *params);

/* Encode BoardHello → output buffer, returns frame length.
 * buf must be at least 64 bytes. */
uint16_t protocol_encode_hello(const protocol_hello_t *hello, uint8_t *buf);

/* Encode NonceResult → output buffer, returns frame length.
 * buf must be at least 128 bytes. */
uint16_t protocol_encode_nonce(const bm1366_result_t *result,
                               uint64_t board_id, uint8_t *buf);

/* Encode AsicRegister → output buffer, returns frame length. */
uint16_t protocol_encode_asic_reg(uint8_t asic_nr, uint8_t reg_type,
                                  uint32_t value, uint8_t *buf);

/* Encode Ack → output buffer, returns frame length. */
uint16_t protocol_encode_ack(uint8_t ack_type, uint8_t *buf);

/* Encode Error → output buffer, returns frame length. */
uint16_t protocol_encode_error(uint8_t code, const char *msg, uint8_t *buf);

/* Read a complete framed message from a buffer.
 * Returns: type byte (0 if incomplete/invalid), sets *payload_len.
 * Caller manages the receive buffer externally. */
uint8_t protocol_peek_frame(const uint8_t *buf, uint16_t buf_len,
                            uint16_t *frame_len, uint16_t *payload_len);

#endif /* __PROTOCOL_H */
