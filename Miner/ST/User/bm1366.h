#ifndef __BM1366_H
#define __BM1366_H

#include "stm32f10x.h"
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
#define BM1366_FREQ_MULT     50.0f           /* 频率倍数 */
#define BM1366_DEFAULT_FREQ  485.0f          /* 默认 485 MHz */

/* BM1366 作业包结构 (打包, 80 字节 payload) */
#pragma pack(push, 1)
typedef struct {
    uint8_t  job_id;
    uint8_t  num_midstates;       /* 1 或 4 */
    uint32_t starting_nonce;      /* 小端 */
    uint32_t nbits;               /* 小端 */
    uint32_t ntime;               /* 小端 */
    uint8_t  merkle_root[32];     /* ASIC 字节序 */
    uint8_t  prev_block_hash[32]; /* ASIC 字节序 */
    uint32_t version;             /* 小端 */
} bm1366_job_t;

/* BM1366 结果包结构 (UART 接收的 12 字节帧) */
typedef struct {
    uint16_t preamble;      /* 0xAA55 */
    uint32_t nonce;         /* 大端 */
    uint8_t  midstate_num;  /* 使用的 midstate 编号 (0-3) */
    uint8_t  job_id_raw;    /* job_id = job_id_raw & 0xF8 */
    uint16_t version_raw;   /* version_bits = ntohs(version_raw) << 13 */
    uint8_t  crc_and_flags; /* bit7=is_job_response, bits4:0=crc5 */
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

/* ===== PLL 参数计算结果 ===== */
typedef struct {
    uint8_t  fb_divider;
    uint8_t  refdiv;
    uint8_t  postdiv1;
    uint8_t  postdiv2;
    float    actual_freq;
} bm1366_pll_params_t;

/* ===== 公开函数 ===== */

/* UART 初始化 (USART1: PA9=TX, PA10=RX, 115200 baud) */
void bm1366_uart_init(void);

/* 切换 UART 波特率 */
void bm1366_uart_set_baud(uint32_t baud);

/* UART 发送原始数据 */
void bm1366_uart_send(const uint8_t *data, uint16_t len);

/* UART 接收 (非阻塞, 0=无数据) */
uint16_t bm1366_uart_recv(uint8_t *buf, uint16_t max_len);

/* UART 接收可用数据量 */
uint16_t bm1366_uart_available(void);

/* 清空 UART 接收缓冲区 */
void bm1366_uart_flush(void);

/* USART1 RX 中断处理函数 (在 stm32f10x_it.c 中调用) */
void bm1366_uart_isr_handler(void);

/* ===== BM1366 高层函数 ===== */

/* 发送命令包 (带 CRC5) */
void bm1366_send_cmd(uint8_t header, const uint8_t *data, uint8_t len);

/* 发送作业包 (带 CRC16) */
void bm1366_send_job(const bm1366_job_t *job);

/* 发送简单指令 (原始字节 + 无附加 CRC, 用于特殊初始化序列) */
void bm1366_send_raw(const uint8_t *data, uint8_t len);

/* 等待并读取一个 ASIC 结果帧 (阻塞, timeout_ms) */
int  bm1366_read_result(bm1366_result_raw_t *result, uint32_t timeout_ms);

/* 初始化 BM1366 芯片链 (返回检测到的芯片数) */
int  bm1366_init_chips(uint8_t expected_count, float target_freq_mhz);

/* 设置芯片频率 (MHz) */
float bm1366_set_frequency(float target_freq_mhz);
void bm1366_frequency_transition(float target_freq_mhz, uint32_t step_delay_ms);
void bm1366_set_voltage(uint8_t vdo_scale);

/* 设置 version_mask (BIP-310) */
void bm1366_set_version_mask(uint32_t mask);

/* 设置 nonce 空间 (HCN = hash counting number) */
void bm1366_set_nonce_space(double nonce_percent, float frequency,
                             uint16_t asic_count, uint16_t cores);

/* 设置难度掩码 */
void bm1366_set_difficulty_mask(uint16_t difficulty);

/* 读取芯片寄存器 */
void bm1366_read_registers(void);

/* 统计芯片数量 */
int  bm1366_count_chips(uint8_t expected_count);

/* 获取地址间隔 */
uint8_t bm1366_get_address_interval(void);

/* 获取实际检测到的芯片数 */
uint8_t bm1366_get_chip_count(void);

/* CRC 计算（公开，因为 protocol 层也可能需要） */
uint8_t  bm1366_crc5(const uint8_t *data, uint8_t len);
uint16_t bm1366_crc16(const uint8_t *data, uint16_t len);

/* 计算 PLL 参数 (目标频率 → fb_divider/refdiv/postdiv1/postdiv2) */
bm1366_pll_params_t bm1366_pll_calc(float target_freq_mhz, uint8_t ref_min, uint8_t ref_max);

#endif /* __BM1366_H */
