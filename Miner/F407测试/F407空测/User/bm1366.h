 #ifndef __BM1366_H
 #define __BM1366_H
 
 #if defined(STM32F40_41xxx)
#include "stm32f4xx.h"
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
#include "stm32f10x.h"
#else
#include "stm32f4xx.h"
#endif
 #include <stdint.h>
 
 /* ===== BM1366 常量 ===== */
 #define BM1366_CHIP_ID         0x1366
 #define BM1366_CHIP_ID_RESP_LEN 11
 
 #define BM1366_TYPE_JOB   0x20
 #define BM1366_TYPE_CMD   0x40
 #define BM1366_GROUP_SINGLE 0x00
 #define BM1366_GROUP_ALL    0x10
 
 #define BM1366_CMD_SETADDRESS 0x00
 #define BM1366_CMD_WRITE      0x01
 #define BM1366_CMD_READ       0x02
 #define BM1366_CMD_INACTIVE   0x03
 
 #define BM1366_NONCE_SPACE   0x100000000ULL  /* 2^32 */
 #define BM1366_FREQ_MULT     50.0f
 #define BM1366_DEFAULT_FREQ  485.0f
 
 /* BM1366 作业包结构 */
 #pragma pack(push, 1)
 typedef struct {
     uint8_t  job_id;
     uint8_t  num_midstates;
     uint32_t starting_nonce;
     uint32_t nbits;
     uint32_t ntime;
     uint8_t  merkle_root[32];
     uint8_t  prev_block_hash[32];
     uint32_t version;
 } bm1366_job_t;
 
 /* BM1366 结果包结构 */
 typedef struct {
     uint16_t preamble;
     uint32_t nonce;
     uint8_t  midstate_num;
     uint8_t  job_id_raw;
     uint16_t version_raw;
     uint8_t  crc_and_flags;
 } bm1366_result_raw_t;
 #pragma pack(pop)
 
 /* 解析后的结果 */
 typedef struct {
     uint8_t  job_id;
     uint8_t  asic_nr;
     uint8_t  core_id;
     uint8_t  small_core_id;
     uint32_t nonce;
     uint32_t rolled_version;
 } bm1366_result_t;
 
 typedef struct {
     uint8_t  fb_divider;
     uint8_t  refdiv;
     uint8_t  postdiv1;
     uint8_t  postdiv2;
     float    actual_freq;
 } bm1366_pll_params_t;
 
 /* ===== 公开函数 ===== */
 void bm1366_uart_init(void);
 void bm1366_uart_set_baud(uint32_t baud);
 void bm1366_uart_send(const uint8_t *data, uint16_t len);
 uint16_t bm1366_uart_recv(uint8_t *buf, uint16_t max_len);
 uint16_t bm1366_uart_available(void);
 void bm1366_uart_flush(void);
 void bm1366_uart_isr_handler(void);
 
 void bm1366_send_cmd(uint8_t header, const uint8_t *data, uint8_t len);
 void bm1366_send_job(const bm1366_job_t *job);
 void bm1366_send_raw(const uint8_t *data, uint8_t len);
 int  bm1366_read_result(bm1366_result_raw_t *result, uint32_t timeout_ms);
 int  bm1366_init_chips(uint8_t expected_count, float target_freq_mhz);
 float bm1366_set_frequency(float target_freq_mhz);
 void bm1366_frequency_transition(float target_freq_mhz, uint32_t step_delay_ms);
 void bm1366_set_voltage(uint8_t vdo_scale);
 void bm1366_set_version_mask(uint32_t mask);
 void bm1366_set_nonce_space(double nonce_percent, float frequency, uint16_t asic_count, uint16_t cores);
 void bm1366_set_difficulty_mask(uint16_t difficulty);
 void bm1366_read_registers(void);
 int  bm1366_count_chips(uint8_t expected_count);
 uint8_t bm1366_get_address_interval(void);
 uint8_t bm1366_get_chip_count(void);
 uint8_t  bm1366_crc5(const uint8_t *data, uint8_t len);
 uint16_t bm1366_crc16(const uint8_t *data, uint16_t len);
 bm1366_pll_params_t bm1366_pll_calc(float target_freq_mhz, uint8_t ref_min, uint8_t ref_max);
 
 #endif
