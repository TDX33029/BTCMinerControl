 #include "bm1366.h"
 #include "Delay.h"
 #include <string.h>
 #include <math.h>
 
 #define BM1366_DEFAULT_BAUD  115200
 #define BM1366_MAX_BAUD      1000000
 #define BM1366_UART           USART1
 #define BM1366_TIMEOUT_MS    100
 #define BM1366_RST_PIN       GPIO_Pin_0
 #define BM1366_RST_PORT      GPIOB
 #define BM1366_CORES         112
 #define BM1366_SMALL_CORES   8
 
 static uint8_t  bm1366_chip_count       = 0;
 static uint8_t  bm1366_address_interval = 0;
 static uint32_t bm1366_cur_baud         = BM1366_DEFAULT_BAUD;
 
 /* ===== CRC5 Table ===== */
 static const uint8_t CRC5_TABLE[256] = {
     0x00,0x05,0x0A,0x0F,0x14,0x11,0x1E,0x1B,0x08,0x0D,0x02,0x07,0x1C,0x19,0x16,0x13,
     0x10,0x15,0x1A,0x1F,0x04,0x01,0x0E,0x0B,0x18,0x1D,0x12,0x17,0x0C,0x09,0x06,0x03,
     0x00,0x05,0x0A,0x0F,0x14,0x11,0x1E,0x1B,0x08,0x0D,0x02,0x07,0x1C,0x19,0x16,0x13,
     0x10,0x15,0x1A,0x1F,0x04,0x01,0x0E,0x0B,0x18,0x1D,0x12,0x17,0x0C,0x09,0x06,0x03,
     0x00,0x05,0x0A,0x0F,0x14,0x11,0x1E,0x1B,0x08,0x0D,0x02,0x07,0x1C,0x19,0x16,0x13,
     0x10,0x15,0x1A,0x1F,0x04,0x01,0x0E,0x0B,0x18,0x1D,0x12,0x17,0x0C,0x09,0x06,0x03,
     0x00,0x05,0x0A,0x0F,0x14,0x11,0x1E,0x1B,0x08,0x0D,0x02,0x07,0x1C,0x19,0x16,0x13,
     0x10,0x15,0x1A,0x1F,0x04,0x01,0x0E,0x0B,0x18,0x1D,0x12,0x17,0x0C,0x09,0x06,0x03,
     0x00,0x05,0x0A,0x0F,0x14,0x11,0x1E,0x1B,0x08,0x0D,0x02,0x07,0x1C,0x19,0x16,0x13,
     0x10,0x15,0x1A,0x1F,0x04,0x01,0x0E,0x0B,0x18,0x1D,0x12,0x17,0x0C,0x09,0x06,0x03,
     0x00,0x05,0x0A,0x0F,0x14,0x11,0x1E,0x1B,0x08,0x0D,0x02,0x07,0x1C,0x19,0x16,0x13,
     0x10,0x15,0x1A,0x1F,0x04,0x01,0x0E,0x0B,0x18,0x1D,0x12,0x17,0x0C,0x09,0x06,0x03,
     0x00,0x05,0x0A,0x0F,0x14,0x11,0x1E,0x1B,0x08,0x0D,0x02,0x07,0x1C,0x19,0x16,0x13,
     0x10,0x15,0x1A,0x1F,0x04,0x01,0x0E,0x0B,0x18,0x1D,0x12,0x17,0x0C,0x09,0x06,0x03,
     0x00,0x05,0x0A,0x0F,0x14,0x11,0x1E,0x1B,0x08,0x0D,0x02,0x07,0x1C,0x19,0x16,0x13,
     0x10,0x15,0x1A,0x1F,0x04,0x01,0x0E,0x0B,0x18,0x1D,0x12,0x17,0x0C,0x09,0x06,0x03,
 };
 
 /* ===== UART Ring Buffer ===== */
 #define UART_RX_BUF_SIZE  256
 static uint8_t  uart_rx_buf[UART_RX_BUF_SIZE];
 static volatile uint16_t uart_rx_head = 0;
 static volatile uint16_t uart_rx_tail = 0;
 
 static int uart_rb_get(uint8_t *byte) {
     if (uart_rx_head == uart_rx_tail) return 0;
     *byte = uart_rx_buf[uart_rx_tail];
     uart_rx_tail = (uart_rx_tail + 1) % UART_RX_BUF_SIZE;
     return 1;
 }
 
 static void uart_rb_put(uint8_t byte) {
     uint16_t next = (uart_rx_head + 1) % UART_RX_BUF_SIZE;
     if (next != uart_rx_tail) {
         uart_rx_buf[uart_rx_head] = byte;
         uart_rx_head = next;
     }
 }
 
 uint16_t bm1366_uart_available(void) {
     return (uart_rx_head >= uart_rx_tail)
            ? (uart_rx_head - uart_rx_tail)
            : (UART_RX_BUF_SIZE - uart_rx_tail + uart_rx_head);
 }
 
 /* ===== USART1 ISR (F4) ===== */
 void bm1366_uart_isr_handler(void) {
     if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
         uint8_t byte = (uint8_t)USART_ReceiveData(USART1);
         uart_rb_put(byte);
     }
 }
 
 /* ===== UART API (F4: GPIO on AHB1, USART on APB2, AF7 for USART1) ===== */
 void bm1366_uart_init(void) {
     GPIO_InitTypeDef gpio;
     USART_InitTypeDef usart;
     NVIC_InitTypeDef nvic;
 
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

#if defined(STM32F40_41xxx)
    /* F4: GPIO on AHB1, use PinAFConfig + Mode_AF + OType/PuPd */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);

    /* TX: PA9 = AF push-pull */
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &gpio);

    /* RX: PA10 = AF input (floating) */
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_AF;
    gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    /* F1: GPIO on APB2, use Mode_AF_PP / Mode_IN_FLOATING (no PinAFConfig) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* TX: PA9 = AF push-pull */
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* RX: PA10 = floating input (USART1 default) */
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);
#endif
     /* RX: PA10 = AF input (floating) */
     gpio.GPIO_Pin = GPIO_Pin_10;
     gpio.GPIO_Mode = GPIO_Mode_AF;
     gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
     gpio.GPIO_Speed = GPIO_Speed_50MHz;
     GPIO_Init(GPIOA, &gpio);
 
     USART_StructInit(&usart);
     usart.USART_BaudRate = 115200;
     usart.USART_WordLength = USART_WordLength_8b;
     usart.USART_StopBits = USART_StopBits_1;
     usart.USART_Parity = USART_Parity_No;
     usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
     usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
     USART_Init(USART1, &usart);
 
     USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
 
     nvic.NVIC_IRQChannel = USART1_IRQn;
     nvic.NVIC_IRQChannelPreemptionPriority = 1;
     nvic.NVIC_IRQChannelSubPriority = 0;
     nvic.NVIC_IRQChannelCmd = ENABLE;
     NVIC_Init(&nvic);
 
     USART_Cmd(USART1, ENABLE);
     bm1366_cur_baud = 115200;
 }
 
 void bm1366_uart_set_baud(uint32_t baud) {
     while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
     USART_Cmd(USART1, DISABLE);
     /* F4: USART1 on APB2 @ 84MHz */
#if defined(STM32F40_41xxx)
    /* F4: USART1 on APB2 @ 84MHz */
    USART1->BRR = (uint32_t)(84000000UL / baud);
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    /* F1: USART1 on APB2 @ 72MHz */
    USART1->BRR = (uint32_t)(72000000UL / baud);
#else
    USART1->BRR = (uint32_t)(72000000UL / baud);
#endif
     USART_Cmd(USART1, ENABLE);
     bm1366_cur_baud = baud;
 }
 
 void bm1366_uart_send(const uint8_t *data, uint16_t len) {
     for (uint16_t i = 0; i < len; i++) {
         while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
         USART_SendData(USART1, data[i]);
     }
     while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
 }
 
 uint16_t bm1366_uart_recv(uint8_t *buf, uint16_t max_len) {
     uint16_t count = 0;
     while (count < max_len && uart_rb_get(&buf[count])) count++;
     return count;
 }
 
 void bm1366_uart_flush(void) {
     uint8_t dummy;
     while (uart_rb_get(&dummy));
 }
 
 /* ===== CRC ===== */
 uint8_t bm1366_crc5(const uint8_t *data, uint8_t len) {
     uint8_t crc = 0x1F;
     for (uint8_t i = 0; i < len; i++) crc = CRC5_TABLE[(crc ^ data[i])];
     return crc;
 }
 
 uint16_t bm1366_crc16(const uint8_t *data, uint16_t len) {
     uint16_t crc = 0xFFFF;
     for (uint16_t i = 0; i < len; i++) {
         crc ^= (uint16_t)data[i] << 8;
         for (uint8_t j = 0; j < 8; j++) {
             if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
             else crc <<= 1;
         }
     }
     return crc;
 }
 
 /* ===== Packet construction ===== */
 static void _send_packet(uint8_t header, const uint8_t *data, uint8_t data_len, int is_job) {
     uint8_t total_len = is_job ? (data_len + 6) : (data_len + 5);
     uint8_t buf[256];
     buf[0] = 0x55; buf[1] = 0xAA;
     buf[2] = header;
     buf[3] = is_job ? (data_len + 4) : (data_len + 3);
     memcpy(buf + 4, data, data_len);
     if (is_job) {
         uint16_t crc = bm1366_crc16(buf + 2, data_len + 2);
         buf[4 + data_len] = (uint8_t)(crc >> 8);
         buf[5 + data_len] = (uint8_t)(crc & 0xFF);
     } else {
         buf[4 + data_len] = bm1366_crc5(buf + 2, data_len + 2);
     }
     bm1366_uart_send(buf, total_len);
 }
 
 void bm1366_send_cmd(uint8_t header, const uint8_t *data, uint8_t len) {
     _send_packet(header, data, len, 0);
 }
 
 void bm1366_send_job(const bm1366_job_t *job) {
     _send_packet(0x20 | 0x00 | 0x01, (const uint8_t *)job, sizeof(bm1366_job_t), 1);
 }
 
 void bm1366_send_raw(const uint8_t *data, uint8_t len) {
     bm1366_uart_send(data, len);
 }
 
 int bm1366_read_result(bm1366_result_raw_t *result, uint32_t timeout_ms) {
     uint8_t buf[12]; uint8_t idx = 0;
     extern volatile uint32_t g_ms;
     uint32_t start = g_ms;
     while (idx < 12) {
         if ((g_ms - start) >= timeout_ms) return 0;
         uint8_t byte;
         if (!uart_rb_get(&byte)) continue;
         if (idx == 0) { if (byte == 0xAA) buf[idx++] = byte; }
         else if (idx == 1) { if (byte == 0x55) buf[idx++] = byte; else { buf[0] = byte; idx = (byte == 0xAA) ? 1 : 0; } }
         else { buf[idx++] = byte; }
     }
     uint8_t calc_crc = bm1366_crc5(buf + 2, 8);
     uint8_t rx_crc = buf[10] & 0x1F;
     if (calc_crc != rx_crc) return 0;
     memcpy(result, buf, 12);
     return 1;
 }
 
 static uint16_t next_power_of_two(uint16_t v) {
     v--;
     v |= v >> 1;
     v |= v >> 2;
     v |= v >> 4;
     v |= v >> 8;
     return v + 1;
 }
 
 static void bm1366_get_difficulty_mask(uint16_t difficulty, uint8_t mask[6]) {
     mask[0] = 0x00;
     mask[1] = 0x14;
     mask[2] = 0x00;
     mask[3] = 0x00;
     mask[4] = (uint8_t)((difficulty >> 8) & 0xFF);
     mask[5] = (uint8_t)(difficulty & 0xFF);
 }
 
 /* ===== PLL calculation ===== */
 bm1366_pll_params_t bm1366_pll_calc(float target_freq_mhz,
                                      uint8_t ref_min, uint8_t ref_max) {
     bm1366_pll_params_t best = {0};
     best.actual_freq = 0.0f;
 
     for (uint8_t fb = 0; fb <= 255; fb++) {
         for (uint8_t ref = ref_min; ref <= ref_max; ref++) {
             for (uint8_t pd1 = 1; pd1 <= 7; pd1++) {
                 for (uint8_t pd2 = 1; pd2 <= 7; pd2++) {
                     float f = 25.0f * (float)(fb + 1) / (float)((ref + 1) * pd1 * pd2);
                     if (fabsf(f - target_freq_mhz) < fabsf(best.actual_freq - target_freq_mhz)) {
                         best.fb_divider    = fb + 1;
                         best.refdiv        = ref + 1;
                         best.postdiv1      = pd1;
                         best.postdiv2      = pd2;
                         best.actual_freq   = f;
                     }
                 }
             }
         }
     }
     return best;
 }
 
 /* ===== BM1366 high-level functions ===== */
 void bm1366_set_version_mask(uint32_t mask) {
     uint16_t versions_to_roll = (uint16_t)(mask >> 13);
     uint8_t cmd[] = { 0x00, 0xA4, 0x90, 0x00,
         (uint8_t)(versions_to_roll >> 8), (uint8_t)(versions_to_roll & 0xFF) };
     bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_ALL | BM1366_CMD_WRITE, cmd, 6);
 }
 
 static void bm1366_set_hash_counting_number(uint32_t hcn) {
     uint8_t cmd[] = { 0x00, 0x10,
         (uint8_t)(hcn >> 24), (uint8_t)(hcn >> 16),
         (uint8_t)(hcn >> 8), (uint8_t)(hcn & 0xFF) };
     bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_ALL | BM1366_CMD_WRITE, cmd, 6);
 }
 
 void bm1366_set_nonce_space(double nonce_percent, float frequency,
                              uint16_t asic_count, uint16_t cores) {
     uint16_t cores_up       = next_power_of_two(cores);
     uint16_t asic_count_up  = next_power_of_two(asic_count);
     float hcn_space = (float)0x100000000ULL / (float)cores_up / (float)asic_count_up;
     double hcn_max  = (double)hcn_space * (double)BM1366_FREQ_MULT / (double)frequency * 0.5;
     double hcn_frac = nonce_percent * hcn_max;
     uint32_t hcn_reg = (uint32_t)hcn_frac;
     bm1366_set_hash_counting_number(hcn_reg);
 }
 
 void bm1366_set_difficulty_mask(uint16_t difficulty) {
     uint8_t mask[6];
     bm1366_get_difficulty_mask(difficulty, mask);
     bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_ALL | BM1366_CMD_WRITE, mask, 6);
 }
 
 static void bm1366_set_chip_address(uint8_t chip_addr) {
     uint8_t data[] = {chip_addr, 0x00};
     bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_SINGLE | BM1366_CMD_SETADDRESS, data, 2);
 }
 
 static void bm1366_send_chain_inactive(void) {
     uint8_t data[] = {0x00, 0x00};
     bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_ALL | BM1366_CMD_INACTIVE, data, 2);
 }
 
 float bm1366_set_frequency(float target_freq_mhz) {
     bm1366_pll_params_t p = bm1366_pll_calc(target_freq_mhz, 144, 235);
     uint8_t fb   = p.fb_divider;
     uint8_t ref  = p.refdiv;
     uint8_t pd1  = p.postdiv1;
     uint8_t pd2  = p.postdiv2;
     uint8_t vdo_scale = ((uint32_t)fb * 50 / ref >= 2400) ? 0x50 : 0x40;
     uint8_t postdiv   = (uint8_t)(((pd1 - 1) & 0x0F) << 4) | ((pd2 - 1) & 0x0F);
     uint8_t cmd[] = {0x00, 0x08, vdo_scale, fb, ref, postdiv};
     bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_ALL | BM1366_CMD_WRITE, cmd, 6);
     return p.actual_freq;
 }
 
 static void bm1366_frequency_immediate(float target_freq_mhz) {
     bm1366_set_frequency(target_freq_mhz);
 }
 
 int bm1366_count_chips(uint8_t expected_count) {
     uint8_t cmd[] = {0x00, 0x00};
     bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_ALL | BM1366_CMD_READ, cmd, 2);
     uint16_t total_wait = 100;
     uint16_t found = 0;
     extern volatile uint32_t g_ms;
     uint32_t start = g_ms;
     while ((g_ms - start) < total_wait && found < expected_count) {
         uint8_t buf[11];
         uint8_t idx = 0;
         while (idx < 11 && (g_ms - start) < total_wait) {
             uint8_t byte;
             if (!uart_rb_get(&byte)) continue;
             buf[idx++] = byte;
         }
         if (idx == 11) {
             uint16_t chip_id = ((uint16_t)buf[4] << 8) | buf[5];
             if (chip_id == BM1366_CHIP_ID) found++;
         }
     }
     return found;
 }
 
 int bm1366_init_chips(uint8_t expected_count, float target_freq_mhz) {
     GPIO_InitTypeDef gpio;
 
     /* F4: GPIOB clock on AHB1 */
#if defined(STM32F40_41xxx)
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    gpio.GPIO_Mode  = GPIO_Mode_OUT;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL;
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
#endif
     gpio.GPIO_Speed = GPIO_Speed_50MHz;
     GPIO_Init(BM1366_RST_PORT, &gpio);
     GPIO_SetBits(BM1366_RST_PORT, BM1366_RST_PIN);
 
     /* Set version mask */
     for (int i = 0; i < 3; i++) {
         bm1366_set_version_mask(0x1FFFE000);
     }
 
     /* Read chip ID */
     {
         uint8_t init3[]  = {0x55, 0xAA, 0x52, 0x05, 0x00, 0x00, 0x0A};
         bm1366_send_raw(init3, 7);
     }
 
     int chips = bm1366_count_chips(expected_count);
     if (chips == 0) return 0;
     bm1366_chip_count = (uint8_t)chips;
 
     {
         uint8_t init4[]  = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA8, 0x00, 0x07, 0x00, 0x00, 0x03};
         bm1366_send_raw(init4, 11);
     }
     {
         uint8_t init5[]  = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x18, 0xFF, 0x0F, 0xC1, 0x00, 0x00};
         bm1366_send_raw(init5, 11);
     }
 
     bm1366_send_chain_inactive();
 
     bm1366_address_interval = 256 / chips;
     for (uint8_t i = 0; i < chips; i++) {
         bm1366_set_chip_address(i * bm1366_address_interval);
     }
 
     {
         uint8_t init135[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x85, 0x40, 0x0C};
         bm1366_send_raw(init135, 11);
     }
     {
         uint8_t init136[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x80, 0x20, 0x19};
         bm1366_send_raw(init136, 11);
     }
 
     bm1366_set_difficulty_mask(256);
 
     {
         uint8_t init138[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x54, 0x00, 0x00, 0x00, 0x03, 0x1D};
         bm1366_send_raw(init138, 11);
     }
     {
         uint8_t init139[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x58, 0x02, 0x11, 0x11, 0x11, 0x06};
         bm1366_send_raw(init139, 11);
     }
     {
         uint8_t init171[] = {0x55, 0xAA, 0x41, 0x09, 0x00, 0x2C, 0x00, 0x7C, 0x00, 0x03, 0x03};
         bm1366_send_raw(init171, 11);
     }
 
     for (uint8_t i = 0; i < chips; i++) {
         uint8_t addr = i * bm1366_address_interval;
         uint8_t reg_a8[]  = {addr, 0xA8, 0x00, 0x07, 0x01, 0xF0};
         bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_SINGLE | BM1366_CMD_WRITE, reg_a8, 6);
         uint8_t reg_18[]  = {addr, 0x18, 0xF0, 0x00, 0xC1, 0x00};
         bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_SINGLE | BM1366_CMD_WRITE, reg_18, 6);
         uint8_t reg_3c_a[] = {addr, 0x3C, 0x80, 0x00, 0x85, 0x40};
         bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_SINGLE | BM1366_CMD_WRITE, reg_3c_a, 6);
         uint8_t reg_3c_b[] = {addr, 0x3C, 0x80, 0x00, 0x80, 0x20};
         bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_SINGLE | BM1366_CMD_WRITE, reg_3c_b, 6);
         uint8_t reg_3c_c[] = {addr, 0x3C, 0x80, 0x00, 0x82, 0xAA};
         bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_SINGLE | BM1366_CMD_WRITE, reg_3c_c, 6);
     }
 
     float actual_freq = bm1366_set_frequency(target_freq_mhz);
     uint16_t cores = BM1366_CORES;
     bm1366_set_nonce_space(1.0, actual_freq, chips, cores);
 
     {
         uint8_t init795[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA4, 0x90, 0x00, 0xFF, 0xFF, 0x1C};
         bm1366_send_raw(init795, 11);
     }
 
     return chips;
 }
 
 void bm1366_read_registers(void) {
     for (uint8_t reg = 0; reg < 0x8D; reg++) {
         if (reg == 0x4C || reg == 0x88 || reg == 0x89 || reg == 0x8A ||
             reg == 0x8B || reg == 0x8C) {
             uint8_t cmd[] = {0x00, reg};
             bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_ALL | BM1366_CMD_READ, cmd, 2);
             Delay_ms(1);
         }
     }
 }
 
 uint8_t bm1366_get_address_interval(void) { return bm1366_address_interval; }
 uint8_t bm1366_get_chip_count(void) { return bm1366_chip_count; }
 
 #define FREQ_STEP_SIZE  6.25f
 #define FREQ_EPSILON    0.0001f
 
 void bm1366_frequency_transition(float target_freq_mhz, uint32_t step_delay_ms) {
     bm1366_pll_params_t current_pll = bm1366_pll_calc(target_freq_mhz, 144, 235);
     float current_freq = current_pll.actual_freq;
     float diff = target_freq_mhz - current_freq;
     if (diff < 0) diff = -diff;
     if (diff < FREQ_EPSILON) return;
     if (diff < FREQ_STEP_SIZE) { bm1366_set_frequency(target_freq_mhz); return; }
     int direction = (target_freq_mhz > current_freq) ? 1 : -1;
     int current_step = (int)(current_freq / FREQ_STEP_SIZE);
     int target_step  = (int)(target_freq_mhz / FREQ_STEP_SIZE);
     while (current_step != target_step) {
         current_step += direction;
         float step_freq = (float)current_step * FREQ_STEP_SIZE;
         bm1366_set_frequency(step_freq);
         extern volatile uint32_t g_ms;
         uint32_t start = g_ms;
         while ((g_ms - start) < step_delay_ms);
     }
     bm1366_set_frequency(target_freq_mhz);
 }
 
 void bm1366_set_voltage(uint8_t vdo_scale) {
     uint8_t cmd[] = {0x00, 0x08, vdo_scale, 0, 0, 0};
     bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_ALL | BM1366_CMD_WRITE, cmd, 6);
 }
