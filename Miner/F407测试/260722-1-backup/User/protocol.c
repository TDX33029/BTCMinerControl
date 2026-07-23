 #include "protocol.h"
 #include <string.h>
 #include <stdio.h>
 
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
 
 static uint16_t build_frame(uint8_t type, const uint8_t *payload,
                              uint16_t payload_len, uint8_t *buf) {
     write_u32(buf, payload_len + 1);
     buf[4] = type;
     if (payload_len > 0) {
         memcpy(buf + 5, payload, payload_len);
     }
     return payload_len + 5;
 }
 
 int protocol_decode_job(const uint8_t *data, uint16_t len, protocol_job_t *job) {
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
     job->version = read_u32(data + off); off += 4;
     memcpy(job->prev_block_hash, data + off, 32); off += 32;
     memcpy(job->merkle_root,      data + off, 32); off += 32;
     job->ntime          = read_u32(data + off); off += 4;
     job->nbits          = read_u32(data + off); off += 4;
     job->starting_nonce = read_u32(data + off); off += 4;
     return 1;
 }
 
 int protocol_decode_setparams(const uint8_t *data, uint16_t len,
                               protocol_setparams_t *params) {
     if (len < 4) return 0;
     params->freq_mhz   = ((uint16_t)data[0] << 8) | data[1];
     params->voltage_mv = ((uint16_t)data[2] << 8) | data[3];
     return 1;
 }
 
 uint16_t protocol_encode_hello(const protocol_hello_t *hello, uint8_t *buf) {
     uint8_t payload[12];
     write_u64(payload,     hello->board_id);
     payload[8] = hello->asic_count;
     write_u16(payload + 9, hello->fw_version);
     payload[11] = hello->status;
     return build_frame(MSG_BOARD_HELLO, payload, 12, buf);
 }
 
 uint16_t protocol_encode_nonce(const bm1366_result_t *result,
                                uint64_t board_id, uint8_t *buf) {
     (void)board_id;
     uint8_t payload[18];
     payload[0] = result->job_id;
     payload[1] = result->asic_nr;
     write_u32(payload + 2,  result->nonce);
     write_u32(payload + 6,  result->rolled_version);
     extern volatile uint32_t g_ms;
     write_u64(payload + 10, (uint64_t)g_ms * 1000ULL);
     return build_frame(MSG_NONCE_RESULT, payload, 18, buf);
 }
 
 uint16_t protocol_encode_asic_reg(uint8_t asic_nr, uint8_t reg_type,
                                   uint32_t value, uint8_t *buf) {
     uint8_t payload[6];
     payload[0] = asic_nr;
     payload[1] = reg_type;
     write_u32(payload + 2, value);
     return build_frame(MSG_ASIC_REGISTER, payload, 6, buf);
 }
 
 uint16_t protocol_encode_ack(uint8_t ack_type, uint8_t *buf) {
     return build_frame(MSG_ACK, &ack_type, 1, buf);
 }
 
 uint16_t protocol_encode_error(uint8_t code, const char *msg, uint8_t *buf) {
     uint16_t msg_len = (uint16_t)strlen(msg);
     if (msg_len > 120) msg_len = 120;
     uint8_t payload[128];
     payload[0] = code;
     memcpy(payload + 1, msg, msg_len);
     return build_frame(MSG_ERROR, payload, msg_len + 1, buf);
 }
 
 uint8_t protocol_peek_frame(const uint8_t *buf, uint16_t buf_len,
                             uint16_t *frame_len, uint16_t *payload_len) {
     if (buf_len < 5) return 0;
     uint32_t total = read_u32(buf);
     if (total > 2048 || total < 1) return 0;
     if (buf_len < total + 4) return 0;
     *frame_len   = (uint16_t)(total + 4);
     *payload_len = (uint16_t)(total - 1);
     return buf[4];
 }
