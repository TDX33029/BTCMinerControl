 #ifndef __PROTOCOL_H
 #define __PROTOCOL_H
 
 #if defined(STM32F40_41xxx)
#include "stm32f4xx.h"
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
#include "stm32f10x.h"
#else
#include "stm32f4xx.h"
#endif
 #include <stdint.h>
 #include "bm1366.h"
 
 typedef enum {
     MSG_JOB           = 0x01,
     MSG_NONCE_RESULT  = 0x02,
     MSG_ASIC_REGISTER = 0x03,
     MSG_BOARD_HELLO   = 0x04,
     MSG_SET_PARAMS    = 0x05,
     MSG_ACK           = 0x06,
     MSG_ERROR         = 0xFF,
 } protocol_msg_type_t;
 
 typedef struct {
     uint8_t  job_id;
     uint8_t  num_midstates;
     uint8_t  midstates[4][32];
     uint32_t version;
     uint8_t  prev_block_hash[32];
     uint8_t  merkle_root[32];
     uint32_t ntime;
     uint32_t nbits;
     uint32_t starting_nonce;
 } protocol_job_t;
 
 typedef struct {
     uint16_t freq_mhz;
     uint16_t voltage_mv;
 } protocol_setparams_t;
 
 typedef struct {
     uint64_t board_id;
     uint8_t  asic_count;
     uint16_t fw_version;
     uint8_t  status;
 } protocol_hello_t;
 
 int protocol_decode_job(const uint8_t *data, uint16_t len, protocol_job_t *job);
 int protocol_decode_setparams(const uint8_t *data, uint16_t len, protocol_setparams_t *params);
 uint16_t protocol_encode_hello(const protocol_hello_t *hello, uint8_t *buf);
 uint16_t protocol_encode_nonce(const bm1366_result_t *result, uint64_t board_id, uint8_t *buf);
 uint16_t protocol_encode_asic_reg(uint8_t asic_nr, uint8_t reg_type, uint32_t value, uint8_t *buf);
 uint16_t protocol_encode_ack(uint8_t ack_type, uint8_t *buf);
 uint16_t protocol_encode_error(uint8_t code, const char *msg, uint8_t *buf);
 uint8_t protocol_peek_frame(const uint8_t *buf, uint16_t buf_len, uint16_t *frame_len, uint16_t *payload_len);
 
 #endif
