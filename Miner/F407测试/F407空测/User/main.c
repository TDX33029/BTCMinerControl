/**
 * BTCMinerControl — STM32F407ZGT6 (野火霸天虎V2) BM1366矿机桥接固件
 *
 * 功能:
 *   1. 通过内置ETH MAC + LAN8720A PHY + LwIP 连接 PC (BTCMinerControl.exe)
 *   2. 接收PC下发的作业，转发给BM1366 ASIC芯片
 *   3. 收集ASIC nonce结果，回传给PC
 *
 * 硬件:
 *   - STM32F407ZGT6 (168MHz, 192KB SRAM, 1MB Flash)
 *   - LAN8720A PHY (RMII), 网络变压器 + RJ45
 *   - BM1366 ASIC (通过USART1: PA9=TX, PA10=RX, PB0=RST)
 *   - LED: PF9=STATUS, PF10=LINK
 *   - 调试串口: USART6 @ PC6=TX, PC7=RX, 115200 8N1
 */

#include "stm32f4xx.h"
#include "Delay.h"
#include "eth_drv.h"
#include "bm1366.h"
#include "protocol.h"
#include "debug_serial.h"
#include <string.h>

/* ===== 可配置参数 ===== */
#define CFG_MAC0   0x00
#define CFG_MAC1   0x08
#define CFG_MAC2   0xDC
#define CFG_MAC3   0xAB
#define CFG_MAC4   0xCD
#define CFG_MAC5   0xEF

#define CFG_IP0    192
#define CFG_IP1    168
#define CFG_IP2    1
#define CFG_IP3    100   /* 本板IP */

#define CFG_GW0    192
#define CFG_GW1    168
#define CFG_GW2    1
#define CFG_GW3    1

#define CFG_SN0    255
#define CFG_SN1    255
#define CFG_SN2    255
#define CFG_SN3    0

#define CFG_LOCAL_PORT   6000

#define PC_IP0     192
#define PC_IP1     168
#define PC_IP2     1
#define PC_IP3     50
#define PC_PORT    4028

#define BM1366_EXPECTED_COUNT  0
#define BM1366_TARGET_FREQ_MHZ 485.0f

#define FW_VERSION_MAJOR  2
#define FW_VERSION_MINOR  0
#define FW_VERSION        ((FW_VERSION_MAJOR << 8) | FW_VERSION_MINOR)
#define BOARD_ID  0x0000000200000001ULL

/* ===== 全局变量 ===== */
volatile uint32_t g_ms = 0;

static const eth_config_t eth_cfg = {
    .mac    = {CFG_MAC0, CFG_MAC1, CFG_MAC2, CFG_MAC3, CFG_MAC4, CFG_MAC5},
    .ip     = {CFG_IP0,  CFG_IP1,  CFG_IP2,  CFG_IP3},
    .gateway= {CFG_GW0,  CFG_GW1,  CFG_GW2,  CFG_GW3},
    .subnet = {CFG_SN0,  CFG_SN1,  CFG_SN2,  CFG_SN3},
    .local_port = CFG_LOCAL_PORT,
};

static const uint8_t pc_ip[4]   = {PC_IP0, PC_IP1, PC_IP2, PC_IP3};
static const uint16_t pc_port   = PC_PORT;

static int connected = 0;
static int asic_ready = 0;

/* ===== 函数声明 ===== */
static void system_clock_init(void);
static void led_init(void);
static void led_on(void);
static void led_off(void);
static void led_toggle(void);
static void send_board_hello(void);
static void handle_job(const uint8_t *payload, uint16_t len);
static void handle_setparams(const uint8_t *payload, uint16_t len);
static void check_bm1366_results(void);

/* ===== SysTick 中断 ===== */
void SysTick_Handler(void) {
    g_ms++;
}

/* ===== 系统时钟 168MHz (F407: HSE=8MHz) ===== */
static void system_clock_init(void) {
#if defined(STM32F40_41xxx)
    /* =========================================================
     * F407: HSE=8MHz, PLL=336/8/2=168MHz
     * AHB=168MHz, APB2=84MHz, APB1=42MHz
     * ETH REF_CLK from PHY (LAN8720A generates 50MHz from 25MHz XTAL)
     * ======================================================== */
    RCC_DeInit();
    RCC_HSEConfig(RCC_HSE_ON);
    while (RCC_GetFlagStatus(RCC_FLAG_HSERDY) == RESET);
    RCC_PLLConfig(RCC_PLLSource_HSE, 8, 336, 2, 7);
    RCC_PLLCmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);
    FLASH_SetLatency(FLASH_Latency_3);
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK2Config(RCC_HCLK_Div2);
    RCC_PCLK1Config(RCC_HCLK_Div4);
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    while (RCC_GetSYSCLKSource() != 0x08);
    SysTick_Config(168000000 / 1000);
#elif defined(STM32F10X_CL)
    /* =========================================================
     * F107: HSE=xMHz (user: adjust for your board's crystal)
     * Need: SYSCLK=72MHz + ETH 50MHz RMII clock via PLL2
     *
     * Example: HSE=25MHz, PREDIV1=/5=5MHz, PLL=*14=70MHz,
     *          PLL2=/5*10=50MHz (ETH)
     *
     * 用户请根据实际HSE频率调整PREDIV1和PLL倍率
     * ======================================================== */
    RCC_DeInit();
    RCC_HSEConfig(RCC_HSE_ON);
    while (RCC_GetFlagStatus(RCC_FLAG_HSERDY) == RESET);
    /* 用户配置区 — 根据实际HSE频率修改以下值 */
    RCC_PREDIV1Config(RCC_PREDIV1_Source_HSE, RCC_PREDIV1_Div5);  /* e.g. 25MHz/5=5MHz */
    RCC_PLLConfig(RCC_PLLSource_PREDIV1, RCC_PLLMul_14);          /* 5*14=70MHz */
    RCC_PLLCmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);
    FLASH_SetLatency(FLASH_Latency_2);
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div2);
    RCC_PCLK2Config(RCC_HCLK_Div1);
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    while (RCC_GetSYSCLKSource() != 0x08);
    /* PLL2: 50MHz for ETH RMII (DP83848 needs REF_CLK) */
    RCC_PREDIV2Config(RCC_PREDIV2_Div5);
    RCC_PLL2Config(RCC_PLL2Mul_10);
    RCC_PLL2Cmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLL2RDY) == RESET);
    SysTick_Config(72000000 / 1000);
#else
    /* Fallback: basic init, user must configure */
    RCC_DeInit();
    RCC_HSEConfig(RCC_HSE_ON);
    while (RCC_GetFlagStatus(RCC_FLAG_HSERDY) == RESET);
    RCC_SYSCLKConfig(RCC_SYSCLKSource_HSE);
    SysTick_Config(8000000 / 1000);
#endif
}

/* ===== LED 控制 (PF9=STATUS, PF10=LINK) ===== */
static void led_init(void) {
    GPIO_InitTypeDef g;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);

    g.GPIO_Pin   = GPIO_Pin_9 | GPIO_Pin_10;
    g.GPIO_Mode  = GPIO_Mode_OUT;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOF, &g);

    led_off();
}

static void led_on(void)  { GPIO_SetBits(GPIOF, GPIO_Pin_9); }
static void led_off(void) { GPIO_ResetBits(GPIOF, GPIO_Pin_9); GPIO_ResetBits(GPIOF, GPIO_Pin_10); }
static void led_toggle(void) {
    if (GPIO_ReadOutputDataBit(GPIOF, GPIO_Pin_9))
        GPIO_ResetBits(GPIOF, GPIO_Pin_9);
    else
        GPIO_SetBits(GPIOF, GPIO_Pin_9);
}

/* ===== 网络收发缓冲 ===== */
static uint8_t net_rx_buf[2048];
static uint16_t net_rx_len = 0;
static uint8_t net_tx_buf[256];

/* ===== 发送 BoardHello ===== */
static void send_board_hello(void) {
    protocol_hello_t hello;
    hello.board_id   = BOARD_ID;
    hello.asic_count = bm1366_get_chip_count();
    hello.fw_version = FW_VERSION;
    hello.status     = asic_ready ? 0 : 2;
    uint16_t len = protocol_encode_hello(&hello, net_tx_buf);
    eth_send(net_tx_buf, len);
    DebugSerial_Printf("[PROTO] BoardHello sent (ASIC=%d, status=%d)\r\n",
                       hello.asic_count, hello.status);
}

/* ===== 处理作业 ===== */
static void handle_job(const uint8_t *payload, uint16_t len) {
    if (!asic_ready) return;
    protocol_job_t pjob;
    if (!protocol_decode_job(payload, len, &pjob)) return;
    bm1366_job_t job;
    job.job_id         = pjob.job_id;
    job.num_midstates  = pjob.num_midstates;
    job.starting_nonce = pjob.starting_nonce;
    job.nbits          = pjob.nbits;
    job.ntime          = pjob.ntime;
    job.version        = pjob.version;
    memcpy(job.merkle_root,      pjob.merkle_root,      32);
    memcpy(job.prev_block_hash,  pjob.prev_block_hash,  32);
    bm1366_send_job(&job);
    DebugSerial_Printf("[JOB] Job %d sent to ASIC chain\r\n", pjob.job_id);
    led_toggle();
}

/* ===== 处理频率/电压设置 ===== */
static void handle_setparams(const uint8_t *payload, uint16_t len) {
    protocol_setparams_t params;
    if (!protocol_decode_setparams(payload, len, &params)) return;
    DebugSerial_Printf("[PARAM] Set freq=%d MHz, volt=%d mV\r\n",
                       params.freq_mhz, params.voltage_mv);
    (void)params.voltage_mv;
    bm1366_frequency_transition((float)params.freq_mhz, 100);
    bm1366_set_frequency((float)params.freq_mhz);
}

/* ===== 检查BM1366结果 ===== */
static void check_bm1366_results(void) {
    if (!asic_ready) return;
    bm1366_result_raw_t raw;
    if (!bm1366_read_result(&raw, 10)) return;
    if (!(raw.crc_and_flags & 0x80)) return;
    bm1366_result_t result;
    result.job_id = raw.job_id_raw & 0xF8;
    result.nonce  = raw.nonce;
    uint32_t nonce_le = ((raw.nonce & 0xFF) << 24) |
                         (((raw.nonce >> 8) & 0xFF) << 16) |
                         (((raw.nonce >> 16) & 0xFF) << 8) |
                         ((raw.nonce >> 24) & 0xFF);
    uint8_t addr_interval = bm1366_get_address_interval();
    result.asic_nr       = (uint8_t)((nonce_le >> 17) & 0xFF) / addr_interval;
    result.core_id       = (uint8_t)((nonce_le >> 25) & 0x7F);
    result.small_core_id = raw.job_id_raw & 0x07;
    result.rolled_version = 0;
    uint16_t frame_len = protocol_encode_nonce(&result, BOARD_ID, net_tx_buf);
    eth_send(net_tx_buf, frame_len);
    DebugSerial_Printf("[NONCE] ASIC#%d Core#%d nonce=0x%08X\r\n",
                       result.asic_nr, result.core_id, result.nonce);
}

/* ===== 处理PC消息 ===== */
static void process_rx_data(void) {
    while (1) {
        uint16_t frame_len, payload_len;
        uint8_t type = protocol_peek_frame(net_rx_buf, net_rx_len,
                                           &frame_len, &payload_len);
        if (type == 0) break;
        const uint8_t *payload = net_rx_buf + 5;
        switch (type) {
        case MSG_JOB:           DebugSerial_Printf("[NET] RX: MSG_JOB\r\n");
                                handle_job(payload, payload_len); break;
        case MSG_SET_PARAMS:    DebugSerial_Printf("[NET] RX: MSG_SET_PARAMS\r\n");
                                handle_setparams(payload, payload_len); break;
        case MSG_ACK:           break;
        default:                DebugSerial_Printf("[NET] RX: unknown type 0x%02X\r\n", type); break;
        }
        if (frame_len < net_rx_len)
            memmove(net_rx_buf, net_rx_buf + frame_len, net_rx_len - frame_len);
        net_rx_len -= frame_len;
    }
}

/* ===== TCP 数据接收 ===== */
static void receive_tcp_data(void) {
    if (!connected) return;
    if (eth_is_disconnected()) {
        DebugSerial_Printf("[NET] TCP disconnected\r\n");
        connected = 0; led_off(); return;
    }

    uint16_t space = sizeof(net_rx_buf) - net_rx_len;
    if (space < 256) net_rx_len = 0;

    int rx_len = eth_recv(net_rx_buf + net_rx_len,
                          sizeof(net_rx_buf) - net_rx_len - 1);
    if (rx_len > 0) {
        net_rx_len += (uint16_t)rx_len;
        process_rx_data();
        if (GPIO_ReadOutputDataBit(GPIOF, GPIO_Pin_10))
            GPIO_ResetBits(GPIOF, GPIO_Pin_10);
        else
            GPIO_SetBits(GPIOF, GPIO_Pin_10);
    } else if (rx_len < 0) {
        DebugSerial_Printf("[NET] TCP recv error\r\n");
        connected = 0; led_off();
    }
}

/* ===== 主函数 ===== */
int main(void) {
    system_clock_init();
    led_init();

    /* 调试串口初始化 (PC6=TX, PC7=RX, 115200) */
    DebugSerial_Init();
    DebugSerial_Printf("\r\n========================================\r\n");
    DebugSerial_Printf("  BTCMinerControl F407 霸天虎V2 启动\r\n");
    DebugSerial_Printf("  FW v%d.%d, 168MHz\r\n", FW_VERSION_MAJOR, FW_VERSION_MINOR);
    DebugSerial_Printf("========================================\r\n");

    /* 以太网初始化 */
    DebugSerial_Printf("[SYS] ETH GPIO init...\r\n");
    eth_gpio_init();
    eth_reset();
    Delay_ms(100);

    DebugSerial_Printf("[SYS] ETH MAC+PHY init...\r\n");
    if (!eth_init(&eth_cfg)) {
        DebugSerial_Printf("[ERR] ETH init FAILED!\r\n");
        while (1) { led_on(); Delay_ms(200); led_off(); Delay_ms(200); }
    }
    DebugSerial_Printf("[SYS] ETH init OK (IP: %d.%d.%d.%d)\r\n",
                       CFG_IP0, CFG_IP1, CFG_IP2, CFG_IP3);

    /* BM1366 初始化 */
    DebugSerial_Printf("[SYS] BM1366 UART init...\r\n");
    bm1366_uart_init();
    Delay_ms(500);

#if BM1366_EXPECTED_COUNT > 0
    DebugSerial_Printf("[SYS] Detecting BM1366 chips...\r\n");
    int chips = bm1366_init_chips(BM1366_EXPECTED_COUNT, BM1366_TARGET_FREQ_MHZ);
    asic_ready = (chips > 0) ? 1 : 0;
    DebugSerial_Printf("[SYS] BM1366: %d chip(s), ASIC %s\r\n",
                       chips, asic_ready ? "READY" : "NOT FOUND");
#else
    asic_ready = 0;
    DebugSerial_Printf("[SYS] BM1366 disabled (EXPECTED_COUNT=0), ETH test mode\r\n");
#endif

    /* 启动指示 */
    led_on(); Delay_ms(200); led_off(); Delay_ms(200);
    led_on(); Delay_ms(200); led_off(); Delay_ms(200);

    /* 主循环 */
    uint32_t last_reconnect = 0;
    uint32_t last_led = 0;
    uint32_t last_hello = 0;

    while (1) {
        uint32_t now = g_ms;

        /* TCP连接管理 */
        if (!connected && (now - last_reconnect) > 3000) {
            last_reconnect = now;
            DebugSerial_Printf("[NET] Connecting to %d.%d.%d.%d:%d...\r\n",
                               PC_IP0, PC_IP1, PC_IP2, PC_IP3, PC_PORT);
            int ret = eth_connect((uint8_t *)pc_ip, pc_port);
            if (ret == 1) {
                connected = 1;
                led_on();
                GPIO_SetBits(GPIOF, GPIO_Pin_10);
                net_rx_len = 0;
                DebugSerial_Printf("[NET] TCP connected, sending BoardHello...\r\n");
                send_board_hello();
            } else {
                DebugSerial_Printf("[NET] Connect failed (ret=%d), retry in 3s\r\n", ret);
            }
        }

        /* 接收TCP数据 */
        receive_tcp_data();

        /* 检查BM1366结果 */
        check_bm1366_results();

        /* 定期发送Hello心跳 */
        if (connected && (now - last_hello) > 60000) {
            last_hello = now;
            send_board_hello();
        }

        /* LED心跳 */
        if (connected && (now - last_led) > 1000) {
            last_led = now;
            led_toggle();
        }
    }
}