/**
 * BTCMinerControl 鈥?STM32F107VCT6 BM1366 鐭挎満妗ユ帴鍥轰欢
 *
 * 鍔熻兘:
 *   1. 閫氳繃鍐呯疆 ETH MAC + DP83848 PHY + LwIP 杩炴帴 PC (BTCMinerControl.exe)
 *   2. 鎺ユ敹 PC 涓嬪彂鐨勪綔涓氾紝杞彂缁?6 棰?BM1366 ASIC 鑺墖
 *   3. 鏀堕泦 ASIC nonce 缁撴灉锛屽洖浼犵粰 PC
 *
 * 纭欢:
 *   - STM32F107VCT6 (72MHz, 64KB SRAM, 256KB Flash)
 *   - DP83848 PHY (RMII), 缃戠粶鍙樺帇鍣? RJ45
 *   - BM1366 ASIC 脳6 (PA9=TX, PA10=RX, PB12=RST)
 *   - LED (PC13, 鏉胯浇鎸囩ず鐏?
 *
 * 缂栬瘧閫夐」:
 *   - 鏈?LwIP 婧愮爜鏃朵笉瀹氫箟 ETH_NO_LWIP
 *   - 鏃?LwIP 鏃跺畾涔?ETH_NO_LWIP (浠呮祴璇曚覆鍙ｅ拰 BM1366)
 */

#include "stm32f10x.h"
#include "Delay.h"
#include "eth_drv.h"
#include "bm1366.h"
#include "protocol.h"
#include <string.h>

/* ===== 鍙厤缃弬鏁?(缂栬瘧鏈熷父閲? ===== */

/* 浠ュお缃戦厤缃?*/
#define CFG_MAC0   0x00
#define CFG_MAC1   0x08
#define CFG_MAC2   0xDC
#define CFG_MAC3   0xAB
#define CFG_MAC4   0xCD
#define CFG_MAC5   0xEF

#define CFG_IP0    192
#define CFG_IP1    168
#define CFG_IP2    1
#define CFG_IP3    100   /* STM32 鏉?IP */

#define CFG_GW0    192
#define CFG_GW1    168
#define CFG_GW2    1
#define CFG_GW3    1     /* 缃戝叧 */

#define CFG_SN0    255
#define CFG_SN1    255
#define CFG_SN2    255
#define CFG_SN3    0

#define CFG_LOCAL_PORT   6000  /* 鏈湴绔彛 */

/* PC (BTCMinerControl) 鍦板潃 */
#define PC_IP0     192
#define PC_IP1     168
#define PC_IP2     1
#define PC_IP3     50
#define PC_PORT    4028    /* BTCMinerControl board_port */

/* BM1366 閰嶇疆 */
#define BM1366_EXPECTED_COUNT  6
#define BM1366_TARGET_FREQ_MHZ 485.0f

/* 鍥轰欢鐗堟湰 */
#define FW_VERSION_MAJOR  1
#define FW_VERSION_MINOR  0
#define FW_VERSION        ((FW_VERSION_MAJOR << 8) | FW_VERSION_MINOR)

/* Board ID (鍙互鐢?STM32 UID 鎴栧叾浠栧敮涓€鏍囪瘑) */
#define BOARD_ID  0x0000000100000001ULL

/* ===== 鍏ㄥ眬鍙橀噺 ===== */

/* SysTick 姣璁℃暟鍣?*/
volatile uint32_t g_ms = 0;

/* ETH  閰嶇疆 */
static const eth_config_t eth_cfg = {
    .mac    = {CFG_MAC0, CFG_MAC1, CFG_MAC2, CFG_MAC3, CFG_MAC4, CFG_MAC5},
    .ip     = {CFG_IP0,  CFG_IP1,  CFG_IP2,  CFG_IP3},
    .gateway= {CFG_GW0,  CFG_GW1,  CFG_GW2,  CFG_GW3},
    .subnet = {CFG_SN0,  CFG_SN1,  CFG_SN2,  CFG_SN3},
    .local_port = CFG_LOCAL_PORT,
};

/* PC 鐩爣鍦板潃 */
static const uint8_t pc_ip[4]   = {PC_IP0, PC_IP1, PC_IP2, PC_IP3};
static const uint16_t pc_port   = PC_PORT;

/* 杩炴帴鐘舵€?*/
static int connected = 0;

/* BM1366 鐘舵€?*/
static int asic_ready = 0;

/* ===== 鍑芥暟澹版槑 ===== */
/* RCC_PLLMul_14 fallback for DFP 2.4.1 */
#ifndef RCC_PLLMul_14
#define RCC_PLLMul_14  ((uint32_t)0x00300000)
#endif
static void system_clock_init(void);
static void led_init(void);
static void led_on(void);
static void led_off(void);
static void led_toggle(void);
static void send_board_hello(void);
static void handle_job(const uint8_t *payload, uint16_t len);
static void handle_setparams(const uint8_t *payload, uint16_t len);
static void check_bm1366_results(void);

/* ===== SysTick 涓柇 ===== */
void SysTick_Handler(void) {
    g_ms++;
}

/* ===== 绯荤粺鏃堕挓 72MHz (F107 CL 绯诲垪) ===== */
static void system_clock_init(void) {
    RCC_DeInit();

    /* 浣胯兘 HSE (25MHz) */
    RCC_HSEConfig(RCC_HSE_ON);
    while (RCC_GetFlagStatus(RCC_FLAG_HSERDY) == RESET);

    /* F107 CL: HSE 鈫?PREDIV1 (/5 = 5MHz) 鈫?PLL (脳9 = 72MHz) 鈫?PLL2 鈫?50MHz MCO
     * 绠€鍖栨柟妗? HSE /5 *9 = 72MHz SYSCLK (淇濆畧, 閬垮厤瓒呴)
     * 瀹為檯鎺ㄨ崘: 閰嶇疆 PLL2 杈撳嚭 50MHz 缁?ETH
     */
    /* PLL = HSE_PREDIV1 * 9 = 5MHz * 9 = 72MHz
     * 娉ㄦ剰: F107 鐨?PREDIV1 鍦?HSE 鍜?PLL 涔嬮棿 */
    RCC_PREDIV1Config(RCC_PREDIV1_Source_HSE, RCC_PREDIV1_Div5);
    RCC_PLLConfig(RCC_PLLSource_PREDIV1, RCC_PLLMul_14);
    RCC_PLLCmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);

    /* 璁剧疆 Flash 寤舵椂 (72MHz 闇€瑕?1 绛夊緟鍛ㄦ湡, 浣嗕负浜?ETH 鐢?2) */
    FLASH_SetLatency(FLASH_Latency_2);

    /* AHB=72MHz, APB1=22.5MHz, APB2=72MHz */
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div2);
    RCC_PCLK2Config(RCC_HCLK_Div1);

    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    while (RCC_GetSYSCLKSource() != 0x08);

    /* PLL2 閰嶇疆: 杈撳嚭 50MHz 缁?ETH MAC (浣跨敤 RMII 闇€瑕?50MHz) */
    RCC_PREDIV2Config(RCC_PREDIV2_Div5);   /* [fix] PREDIV2=div5, PLL2 input=25/5=5MHz */
    RCC_PLL2Config(RCC_PLL2Mul_10);        /* 5MHz *10 = 50MHz for ETH */
    RCC_PLL2Cmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLL2RDY) == RESET);

    /* PLL3 閰嶇疆: 鍙€? 鐢ㄤ簬 USB OTG */

    /* AHB 棰勫垎棰戝櫒: 璁剧疆 ETH 鏃堕挓 = HCLK = 72MHz (闇€瑕?25-50MHz 鑼冨洿) */

    /* SysTick: 1ms */
    SysTick_Config(70000000 / 1000);
}

/* ===== LED 鎺у埗 (PC13, 浣庣數骞充寒) ===== */
static void led_init(void) {
    GPIO_InitTypeDef gpio;

    /* STATUS: PC13 (onboard, low=on) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    gpio.GPIO_Pin   = GPIO_Pin_13;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &gpio);

    /* LINK=PD12, ACT=PD13 */
    gpio.GPIO_Pin   = GPIO_Pin_12 | GPIO_Pin_13;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOD, &gpio);

    led_off();
}

static void led_on(void)  { GPIO_ResetBits(GPIOC, GPIO_Pin_13); }
static void led_off(void) { GPIO_SetBits(GPIOC, GPIO_Pin_13); GPIO_ResetBits(GPIOD, GPIO_Pin_12); GPIO_ResetBits(GPIOD, GPIO_Pin_13); }
static void led_toggle(void) {
    if (GPIO_ReadOutputDataBit(GPIOC, GPIO_Pin_13))
        GPIO_ResetBits(GPIOC, GPIO_Pin_13);
    else
        GPIO_SetBits(GPIOC, GPIO_Pin_13);
}

/* ===== 缃戠粶鏀跺彂缂撳啿鍖?===== */
static uint8_t net_rx_buf[2048];  /* 2KB RX buffer */
static uint16_t net_rx_len = 0;
static uint8_t net_tx_buf[256];   /* TX frame buffer */

/* ===== 鍙戦€?BoardHello ===== */
static void send_board_hello(void) {
    protocol_hello_t hello;
    hello.board_id   = BOARD_ID;
    hello.asic_count = bm1366_get_chip_count();
    hello.fw_version = FW_VERSION;
    hello.status     = asic_ready ? 0 : 2;

    uint16_t len = protocol_encode_hello(&hello, net_tx_buf);
    eth_send(net_tx_buf, len);
}

/* ===== 澶勭悊浣滀笟 (灏?PC 涓嬪彂鐨?Job 杞崲涓?BM1366 鏍煎紡骞跺彂閫? ===== */
static void handle_job(const uint8_t *payload, uint16_t len) {
    if (!asic_ready) return;

    protocol_job_t pjob;
    if (!protocol_decode_job(payload, len, &pjob)) return;

    /* 鏋勫缓 BM1366 浣滀笟鍖?*/
    bm1366_job_t job;
    job.job_id        = pjob.job_id;
    job.num_midstates = pjob.num_midstates;
    job.starting_nonce = pjob.starting_nonce;
    job.nbits          = pjob.nbits;
    job.ntime          = pjob.ntime;
    job.version        = pjob.version;
    memcpy(job.merkle_root,      pjob.merkle_root,      32);
    memcpy(job.prev_block_hash,  pjob.prev_block_hash,  32);

    /* BM1366 鍙渶瑕佺 0 涓?midstate (鐗堟湰杞浆鐢辫姱鐗囩‖浠跺畬鎴? */
    /* 鍙戦€佸埌 ASIC 閾?(骞挎挱, 浣嗘瘡涓姱鐗囨湁鐙珛鐨?nonce 绌洪棿) */
    bm1366_send_job(&job);

    /* LED 闂儊鎻愮ず浣滀笟鎺ユ敹 */
    led_toggle();
}

/* ===== 澶勭悊棰戠巼/鐢靛帇璁剧疆 ===== */
static void handle_setparams(const uint8_t *payload, uint16_t len) {
    protocol_setparams_t params;
    if (!protocol_decode_setparams(payload, len, &params)) return;

    /* Use frequency transition to avoid PLL instability */
    (void)params.voltage_mv;
    bm1366_frequency_transition((float)params.freq_mhz, 100);
    bm1366_set_frequency((float)params.freq_mhz);
}

/* ===== 妫€鏌ュ苟杞彂 BM1366 缁撴灉 ===== */
static void check_bm1366_results(void) {
    if (!asic_ready) return;

    bm1366_result_raw_t raw;
    if (!bm1366_read_result(&raw, 10)) return; /* 10ms 瓒呮椂 */

    /* 妫€鏌ユ槸鍚︽槸浣滀笟鍝嶅簲 (job 缁撴灉鑰岄潪瀵勫瓨鍣ㄨ鍙? */
    if (!(raw.crc_and_flags & 0x80)) return;

    /* 瑙ｆ瀽缁撴灉 */
    bm1366_result_t result;
    result.job_id       = raw.job_id_raw & 0xF8;
    result.nonce        = raw.nonce; /* 淇濇寔缃戠粶瀛楄妭搴?(澶х), PC 绔鐞?*/
    /* asic_nr 浠?nonce 绗?17-24 浣嶆彁鍙?*/
    /* 鎵嬪姩瀛楄妭搴忕炕杞?(閬垮厤 GCC __builtin_bswap32) */
    uint32_t nonce_le = ((raw.nonce & 0xFF) << 24) |
                        (((raw.nonce >> 8) & 0xFF) << 16) |
                        (((raw.nonce >> 16) & 0xFF) << 8) |
                        ((raw.nonce >> 24) & 0xFF);
    uint8_t addr_interval = bm1366_get_address_interval();
    result.asic_nr      = (uint8_t)((nonce_le >> 17) & 0xFF) / addr_interval;
    result.core_id      = (uint8_t)((nonce_le >> 25) & 0x7F);
    result.small_core_id= raw.job_id_raw & 0x07;
    /* version_bits = ntohs(raw.version_raw) << 13 */
    /* 浣嗗湪杞彂鍦烘櫙锛岀洿鎺ュ彂鍘熷 nonce + job_id锛孭C 绔湁瀹屾暣 job 淇℃伅鍙洖鎺?*/
    result.rolled_version = 0; /* PC 绔湁 job 鏁版嵁锛屼笉鍋氭湰鍦拌绠?*/

    /* 缂栫爜涓轰簩杩涘埗甯у苟鍙戦€?*/
    uint16_t frame_len = protocol_encode_nonce(&result, BOARD_ID, net_tx_buf);
    eth_send(net_tx_buf, frame_len);
}

/* ===== 澶勭悊鏉ヨ嚜 PC 鐨勬秷鎭?===== */
static void process_rx_data(void) {
    while (1) {
        uint16_t frame_len, payload_len;
        uint8_t type = protocol_peek_frame(net_rx_buf, net_rx_len,
                                          &frame_len, &payload_len);
        if (type == 0) break;

        /* payload 浠?buf[5] 寮€濮? payload_len = frame_len - 5 */
        const uint8_t *payload = net_rx_buf + 5;

        switch (type) {
        case MSG_JOB:
            handle_job(payload, payload_len);
            break;
        case MSG_SET_PARAMS:
            handle_setparams(payload, payload_len);
            break;
        case MSG_ACK:
            /* PC 绔‘璁? 鏃犻渶澶勭悊 */
            break;
        default:
            break;
        }

        /* 浠庣紦鍐插尯绉婚櫎宸插鐞嗙殑甯?*/
        if (frame_len < net_rx_len) {
            memmove(net_rx_buf, net_rx_buf + frame_len,
                    net_rx_len - frame_len);
        }
        net_rx_len -= frame_len;
    }
}

/* ===== TCP 鏁版嵁鎺ユ敹 ===== */
static void receive_tcp_data(void) {
    if (!connected) return;

    /* 妫€鏌ヨ繛鎺ユ槸鍚︽柇寮€ */
    if (eth_is_disconnected()) {
        connected = 0;
        led_off();
        return;
    }

    /* 鎺ユ敹鏁版嵁骞惰拷鍔犲埌缂撳啿鍖?*/
    uint16_t space = sizeof(net_rx_buf) - net_rx_len;
    if (space < 256) {
        /* 缂撳啿鍖烘帴杩戞弧, 涓㈠純鏃ф暟鎹?(涓嶅簲璇ュ彂鐢? */
        net_rx_len = 0;
    }

    int rx_len = eth_recv(net_rx_buf + net_rx_len,
                            sizeof(net_rx_buf) - net_rx_len - 1);
    if (rx_len > 0) {
        net_rx_len += (uint16_t)rx_len;
        process_rx_data();
        /* ACT toggle on data */
        if (GPIO_ReadOutputDataBit(GPIOD, GPIO_Pin_13))
            GPIO_ResetBits(GPIOD, GPIO_Pin_13);
        else
            GPIO_SetBits(GPIOD, GPIO_Pin_13);
    } else if (rx_len < 0) {
        /* 鏂繛 */
        connected = 0;
        led_off();
    }
}

/* ===== 涓诲嚱鏁?===== */
int main(void) {
    /* 1. 绯荤粺鏃堕挓 */
    system_clock_init();

    /* 2. LED */
    led_init();

    /* 3. 鍩虹寤舵椂 (SysTick 宸插湪 system_clock_init 涓厤缃? */
    /* g_ms 閫氳繃 SysTick 涓柇閫掑 */

    /* 4. SPI1 + ETH 鍒濆鍖?*/
    eth_gpio_init();
    eth_reset();

    /* 绛夊緟 ETH 绋冲畾 */
    Delay_ms(100);

    if (!eth_init(&eth_cfg)) {
        /* ETH  鍒濆鍖栧け璐?鈥?鎸佺画闂儊 LED */
        while (1) {
            led_on();  Delay_ms(200);
            led_off(); Delay_ms(200);
        }
    }

    /* 5. USART1 + BM1366 鍒濆鍖?*/
    bm1366_uart_init();

    /* 绛夊緟 ASIC 涓婄數绋冲畾 */
    Delay_ms(500);

    /* 鍒濆鍖?BM1366 鑺墖閾?*/
    int chips = bm1366_init_chips(BM1366_EXPECTED_COUNT, BM1366_TARGET_FREQ_MHZ);
    asic_ready = (chips > 0) ? 1 : 0;

    /* 6. LED: 蹇€熼棯鐑?= 绛夊緟杩炴帴 */
    led_on();
    Delay_ms(200);
    led_off();
    Delay_ms(200);
    led_on();
    Delay_ms(200);
    led_off();

    /* 7. 涓诲惊鐜?*/
    uint32_t last_reconnect = 0;
    uint32_t last_led       = 0;

    while (1) {
        uint32_t now = g_ms;

        /* TCP 杩炴帴绠＄悊 */
        if (!connected && (now - last_reconnect) > 3000) {
            last_reconnect = now;
            int ret = eth_connect((uint8_t *)pc_ip, pc_port);
            if (ret == 1) {
                connected = 1;
                led_on();                  /* STATUS on */
                GPIO_SetBits(GPIOD, GPIO_Pin_12);  /* LINK on */
                net_rx_len = 0;

                /* 鍙戦€?BoardHello */
                send_board_hello();
            }
        }

        /* 鎺ユ敹 TCP 鏁版嵁 */
        receive_tcp_data();

        /* 妫€鏌?BM1366 缁撴灉 */
        check_bm1366_results();

        /* LED 蹇冭烦 (杩炴帴鐘舵€佷笅鍛ㄦ湡鎬ч棯鐑? */
        if (connected && (now - last_led) > 1000) {
            last_led = now;
            led_toggle();
        }
    }

    return 0;
}
