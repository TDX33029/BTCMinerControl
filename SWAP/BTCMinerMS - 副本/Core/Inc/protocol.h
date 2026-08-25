 #ifndef __PROTOCOL_H
 #define __PROTOCOL_H
 
 #include "main.h"
 #include <stdint.h>
 #include "bm1366.h"
 
 typedef enum {
     MSG_JOB           = 0x01,
     MSG_NONCE_RESULT  = 0x02,
     MSG_ASIC_REGISTER = 0x03,
     MSG_BOARD_HELLO   = 0x04,
     MSG_SET_PARAMS    = 0x05,
     MSG_ACK           = 0x06,
     MSG_BOARD_TELEMETRY = 0x07,
     MSG_SET_POWER      = 0x08,
     MSG_LATENCY_PROBE  = 0x09,
     MSG_SET_VERSION_MASK = 0x0A,
     MSG_HASHRATE      = 0x0B,
     MSG_SET_FREQUENCY = 0x0C,
     MSG_FREQUENCY_STATUS = 0x0D,
     MSG_ERROR         = 0xFF,
 } protocol_msg_type_t;
 
 typedef struct protocol_job_t {
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
     uint32_t version_mask;
 } protocol_version_mask_t;

 typedef struct {
     uint32_t hashrate_mhs;   /* measured hashrate, megahash/s */
 } protocol_hashrate_t;

 typedef struct {
     uint16_t frequency_mhz;
 } protocol_set_frequency_t;

 typedef struct {
     uint16_t target_mhz;
     uint16_t actual_mhz;
 } protocol_frequency_status_t;

 typedef struct {
     uint8_t enabled;
 } protocol_setpower_t;

 typedef struct {
     uint64_t token;
 } protocol_latency_t;
 
 typedef struct {
     uint64_t board_id;
     uint8_t  asic_count;
     uint16_t fw_version;
     uint8_t  status;
     uint16_t target_frequency_mhz;
     uint16_t actual_frequency_mhz;
 } protocol_hello_t;

#define TELEMETRY_FLAG_TPS_DETECTED  0x01U
#define TELEMETRY_FLAG_TMP_DETECTED  0x02U
#define TELEMETRY_FLAG_TPS_VALID     0x04U
#define TELEMETRY_FLAG_TMP_VALID     0x08U
#define TELEMETRY_FLAG_POWER_VALID   0x10U

 typedef struct {
     uint8_t flags;
     uint8_t tps_address;
     uint8_t tmp_address;
     uint8_t power_enabled;
     uint16_t vout_mv;
     uint32_t iout_ma;
     uint32_t power_mw;
     int16_t tmp_temperature_centi_c;
     int16_t tps_temperature_centi_c;
     uint16_t tps_status_word;
 } protocol_telemetry_t;
 
 int protocol_decode_job(const uint8_t *data, uint16_t len, protocol_job_t *job);
 int protocol_decode_setparams(const uint8_t *data, uint16_t len, protocol_setparams_t *params);
 int protocol_decode_version_mask(const uint8_t *data, uint16_t len, protocol_version_mask_t *mask);
 int protocol_decode_set_frequency(const uint8_t *data, uint16_t len, protocol_set_frequency_t *freq);
 int protocol_decode_setpower(const uint8_t *data, uint16_t len, protocol_setpower_t *power);
 int protocol_decode_latency(const uint8_t *data, uint16_t len, protocol_latency_t *latency);
 uint16_t protocol_encode_hello(const protocol_hello_t *hello, uint8_t *buf);
 uint16_t protocol_encode_nonce(const bm1366_result_t *result, uint64_t board_id, uint8_t *buf);
 uint16_t protocol_encode_hashrate(const protocol_hashrate_t *hashrate, uint8_t *buf);
 uint16_t protocol_encode_frequency_status(const protocol_frequency_status_t *freq, uint8_t *buf);
 uint16_t protocol_encode_asic_reg(uint8_t asic_nr, uint8_t reg_type, uint32_t value, uint8_t *buf);
 uint16_t protocol_encode_ack(uint8_t ack_type, uint8_t *buf);
 uint16_t protocol_encode_telemetry(const protocol_telemetry_t *telemetry, uint8_t *buf);
 uint16_t protocol_encode_latency(const protocol_latency_t *latency, uint8_t *buf);
 uint16_t protocol_encode_error(uint8_t code, const char *msg, uint8_t *buf);
 uint8_t protocol_peek_frame(const uint8_t *buf, uint16_t buf_len, uint16_t *frame_len, uint16_t *payload_len);
 
 #endif
