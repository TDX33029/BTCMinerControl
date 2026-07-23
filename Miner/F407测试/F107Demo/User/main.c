/**
 * BTCMinerControl — STM32F107 霸天虎RC BM1366矿机桥接固件
 *
 * 硬件:
 *   - STM32F107RC (24MHz(HSE), 48KB SRAM, 256KB Flash)
 *   - DP83848 PHY (RMII)
 *   - BM1366 ASIC (USART1: PA9=TX, PA10=RX)
 *   - 调试串口: USART2 remap @ PD5=TX, PD6=RX, 115200 8N1
 */

#include "stm32f10x.h"
#include "Delay.h"
#include "eth_drv.h"
#include "bm1366.h"
#include "protocol.h"
#include "debug_serial.h"
#include <string.h>
#include <stdio.h>

/* ===== 可配置参数 ===== */
#define CFG_MAC0   0x00
#define CFG_MAC1   0x08
#define CFG_MAC2   0xDC
#define CFG_MAC3   0xAB
#define CFG_MAC4   0xCD
#define CFG_MAC5   0xEF

#define CFG_IP0    26
#define CFG_IP1    8
#define CFG_IP2    1
#define CFG_IP3    20

#define CFG_GW0    26
#define CFG_GW1    8
#define CFG_GW2    1
#define CFG_GW3    1

#define CFG_SN0    255
#define CFG_SN1    255
#define CFG_SN2    255
#define CFG_SN3    0

#define CFG_LOCAL_PORT   6000

#define PC_IP0     26
#define PC_IP1     1
#define PC_IP2     1
#define PC_IP3     11
#define PC_PORT    4028

#define BM1366_EXPECTED_COUNT  0
#define BM1366_TARGET_FREQ_MHZ 485.0f

#define FW_VERSION_MAJOR  2
#define FW_VERSION_MINOR  0
#define FW_VERSION        ((FW_VERSION_MAJOR << 8) | FW_VERSION_MINOR)
#define BOARD_ID  0x0000000200000001ULL

/* ===== 全局变量 ===== */
volatile uint32_t g_ms = 0;
static int           asic_ready    = 0;
static int           connected     = 0;

static uint8_t pc_ip_arr[4]    = {PC_IP0, PC_IP1, PC_IP2, PC_IP3};
static const uint16_t pc_port  = PC_PORT;

static const eth_config_t eth_cfg = {
    .mac = {CFG_MAC0, CFG_MAC1, CFG_MAC2, CFG_MAC3, CFG_MAC4, CFG_MAC5},
    .ip  = {CFG_IP0, CFG_IP1, CFG_IP2, CFG_IP3},
    .gateway = {CFG_GW0, CFG_GW1, CFG_GW2, CFG_GW3},
    .subnet  = {CFG_SN0, CFG_SN1, CFG_SN2, CFG_SN3},
    .local_port = CFG_LOCAL_PORT
};

static uint8_t  net_rx_buf[2048];
static uint16_t net_rx_len = 0;

/* ===== LED ===== */
#define LED_PORT  GPIOF
#define LED_PIN   GPIO_Pin_9

static void led_init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOF, ENABLE);
    GPIO_InitTypeDef g;
    g.GPIO_Pin = LED_PIN; g.GPIO_Mode = GPIO_Mode_Out_PP; g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &g);
    GPIO_ResetBits(LED_PORT, LED_PIN);
}
static void led_on(void)  { GPIO_SetBits(LED_PORT, LED_PIN); }
static void led_off(void) { GPIO_ResetBits(LED_PORT, LED_PIN); }
static void led_toggle(void) { GPIO_SetBits(LED_PORT, LED_PIN); Delay_ms(50); GPIO_ResetBits(LED_PORT, LED_PIN); }

/* ===== 板级Hello ===== */
static void send_board_hello(void) {
    protocol_hello_t hello;
    hello.board_id   = BOARD_ID;
    hello.fw_version = FW_VERSION;
    hello.asic_count = (uint8_t)asic_ready;
    hello.status     = 0;
    uint8_t buf[64];
    uint16_t len = protocol_encode_hello(&hello, buf);
    if (len > 0) eth_send(buf, len);
}

/* ===== 检查BM1366结果 ===== */
static void check_bm1366_results(void) {
    if (!asic_ready) return;
    bm1366_result_raw_t raw;
    if (bm1366_read_result(&raw, 0) > 0) {
        /* 解析原始结果 */
        bm1366_result_t parsed;
        memset(&parsed, 0, sizeof(parsed));
        parsed.nonce  = raw.nonce;
        parsed.job_id = raw.job_id_raw & 0x7F;
        parsed.asic_nr = 0;
        parsed.core_id = raw.midstate_num;
        parsed.small_core_id = (raw.crc_and_flags >> 4) & 0x0F;
        parsed.rolled_version = ((uint32_t)raw.version_raw) << 16;

        uint8_t buf[64];
        uint16_t len = protocol_encode_nonce(&parsed, BOARD_ID, buf);
        if (len > 0) eth_send(buf, len);
    }
}

/* ===== 接收TCP数据 ===== */
static void receive_tcp_data(void) {
    int len = eth_recv(net_rx_buf, sizeof(net_rx_buf));
    if (len <= 0) return;
    net_rx_len = (uint16_t)len;

    uint16_t frame_len, payload_len;
    uint8_t type = protocol_peek_frame(net_rx_buf, net_rx_len, &frame_len, &payload_len);
    if (type == 0 || frame_len == 0) return;

    switch (type) {
        case MSG_JOB: {
            protocol_job_t job;
            if (protocol_decode_job(net_rx_buf, frame_len, &job) > 0) {
                if (asic_ready) bm1366_send_job((const bm1366_job_t *)&job);
                printf("[JOB] id=%d\r\n", job.job_id);
            }
            break;
        }
        case MSG_SET_PARAMS: {
            protocol_setparams_t params;
            if (protocol_decode_setparams(net_rx_buf, frame_len, &params) > 0) {
                if (asic_ready) {
                    bm1366_frequency_transition((float)params.freq_mhz, 100);
                }
                printf("[PARAM] freq=%d, volt=%d\r\n", params.freq_mhz, params.voltage_mv);
            }
            break;
        }
        default:
            break;
    }
}

/* ===== main ===== */
int main(void) {
    /* Serial first - before anything else */
    DebugSerial_Init();
    printf("B...\r\n");
    
    led_init();

    printf("\r\n========================================\r\n");
    printf("  BTCMinerControl F107 霸天虎RC 启动\r\n");
    printf("  FW v%d.%d, 24MHz\r\n", FW_VERSION_MAJOR, FW_VERSION_MINOR);
    printf("========================================\r\n");

    /* ETH */
    printf("[SYS] ETH GPIO init...\r\n");
    eth_gpio_init();
    eth_reset();
    Delay_ms(100);

    printf("[SYS] ETH MAC+PHY init...\r\n");
    if (!eth_init(&eth_cfg)) {
        printf("[ERR] ETH init FAILED!\r\n");
        while (1) { led_on(); Delay_ms(200); led_off(); Delay_ms(200); }
    }
    printf("[SYS] ETH init OK (IP: %d.%d.%d.%d)\r\n",
                       CFG_IP0, CFG_IP1, CFG_IP2, CFG_IP3);

    /* BM1366 */
#if BM1366_EXPECTED_COUNT > 0
    printf("[SYS] BM1366 UART init...\r\n");
    bm1366_uart_init();
    Delay_ms(500);
    printf("[SYS] Detecting BM1366 chips...\r\n");
    int chips = bm1366_init_chips(BM1366_EXPECTED_COUNT, BM1366_TARGET_FREQ_MHZ);
    asic_ready = (chips > 0) ? 1 : 0;
    printf("[SYS] BM1366: %d chip(s), ASIC %s\r\n",
                       chips, asic_ready ? "READY" : "NOT FOUND");
#else
    asic_ready = 0;
    printf("[SYS] BM1366 disabled (EXPECTED_COUNT=0), ETH test mode\r\n");
#endif

    led_on(); Delay_ms(200); led_off(); Delay_ms(200);

    /* 主循环 */
    uint32_t last_reconnect = 0;
    uint32_t last_led = 0;
    uint32_t last_hello = 0;

    while (1) {
        uint32_t now = g_ms;

        if (!connected && (now - last_reconnect) > 3000) {
            last_reconnect = now;
            printf("[NET] Connecting to %d.%d.%d.%d:%d...\r\n",
                               PC_IP0, PC_IP1, PC_IP2, PC_IP3, PC_PORT);
            int ret = eth_connect(pc_ip_arr, pc_port);
            if (ret == 1) {
                connected = 1;
                led_on();
                net_rx_len = 0;
                printf("[NET] TCP connected\r\n");
                send_board_hello();
            } else {
                printf("[NET] Connect failed (ret=%d)\r\n", ret);
            }
        }

        receive_tcp_data();
        check_bm1366_results();

        if (connected && (now - last_hello) > 60000) {
            last_hello = now;
            send_board_hello();
        }
        if (connected && (now - last_led) > 1000) {
            last_led = now;
            led_toggle();
        }
    }
}
