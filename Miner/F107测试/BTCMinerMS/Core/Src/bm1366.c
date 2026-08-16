 #include "bm1366.h"
 #include "protocol.h"
 #include "Delay.h"
 #include <string.h>
 #include <math.h>
 
 #define BM1366_DEFAULT_BAUD  115200
 #define BM1366_MAX_BAUD      1000000
 #define BM1366_UART           USART1
 #define BM1366_TIMEOUT_MS    100
 #define BM1366_RST_PIN       GPIO_PIN_0
 #define BM1366_RST_PORT      GPIOB
 #define BM1366_CORES         112
 #define BM1366_SMALL_CORES   8
 
 static uint8_t  bm1366_chip_count       = 0;
 static uint8_t  bm1366_address_interval = 0;
 
 /* ===== CRC5 (bit-by-bit, matches ESP-Miner crc.c) =====
  * The old 256-entry table had period 32 (duplicated 8x), ignoring bits 5-7
  * of every byte -> every CMD packet had a wrong CRC5 and was rejected. */
 
 /* ===== UART Ring Buffer ===== */
 #define UART_RX_BUF_SIZE  1024
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
 
 /* ===== USART1 ISR (F1/F107) ===== */
/* DIAG: FE/NE (garbled frame) and ORE (dropped byte) counters -- smoking
   gun for signal-integrity problems on the ASIC UART line. */
static volatile uint16_t uart_rx_frame_errors = 0;
static volatile uint16_t uart_rx_overruns     = 0;
/* DIAG: result-frame drop counters -- nonzero growth means the chip IS
   sending results but they fail our preamble/CRC checks. */
static volatile uint32_t rx_bad_preamble = 0;
static volatile uint32_t rx_bad_crc      = 0;
static volatile uint32_t uart_rx_total   = 0;  /* every byte the ISR accepted */
 void bm1366_uart_isr_handler(void) {
     /* Read SR once. RXNEIE also fires on overrun (ORE); if ORE is set with
        RXNE clear we must clear it by reading SR then DR, otherwise the
        interrupt re-fires forever and freezes the system. */
     uint32_t sr = USART1->SR;
     if (sr & USART_SR_RXNE) {
         uint8_t byte = (uint8_t)(USART1->DR);   /* clears RXNE (and ORE, since SR was read) */
        if (sr & (USART_SR_FE | USART_SR_NE)) uart_rx_frame_errors++;
        if (sr & USART_SR_ORE) uart_rx_overruns++;
         uart_rx_total++;   /* DIAG: total bytes accepted by the ISR */
        uart_rb_put(byte);
     } else if (sr & USART_SR_ORE) {
         (void)USART1->DR;                        /* clear overrun */
        uart_rx_overruns++;
     }
 }
 
 /* ===== UART API (F1/F107: GPIO on AHB1, USART on APB2, AF7 for USART1) ===== */
 extern UART_HandleTypeDef huart1;

void bm1366_uart_init(void) {
    /* CubeMX MX_USART1_UART_Init() already configures USART1 at 115200.
       Just enable RXNE interrupt for BM1366 protocol handler. */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}
 
 void bm1366_uart_set_baud(uint32_t baud) {
    while (!(USART1->SR & USART_SR_TC));
    USART1->CR1 &= ~USART_CR1_UE;
    USART1->BRR = (uint32_t)(HAL_RCC_GetPCLK2Freq() / baud);
    USART1->CR1 |= USART_CR1_UE;
}
 
 void bm1366_uart_send(const uint8_t *data, uint16_t len) {
     for (uint16_t i = 0; i < len; i++) {
         while ((USART1->SR & USART_SR_TXE) == RESET);   /* wait until TDR is empty */
         USART1->DR = data[i];
     }
     while ((USART1->SR & USART_SR_TC) == RESET);        /* wait until frame is shifted out */
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
 /* Bit-by-bit CRC5, init 0x1F, poly x^5+x^2+1, MSB-first -- matches ESP-Miner. */
 uint8_t bm1366_crc5(const uint8_t *data, uint8_t len) {
     uint8_t crc = 0x1F;
     for (uint8_t i = 0; i < len; i++) {
         uint8_t byte = data[i];
         for (uint8_t b = 0; b < 8; b++) {
             uint8_t bit = (byte >> 7) & 1;
             byte <<= 1;
             uint8_t new_bit = ((crc >> 4) ^ bit) & 1;
             crc = ((crc << 1) | new_bit) ^ (new_bit << 2);
             crc &= 0x1F;
         }
     }
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
 
 void bm1366_send_job(const struct protocol_job_t *job) {
     /* Build the BM1366 work packet data (big-endian on the wire, matching
        the register writes elsewhere in this file):
          chip_addr | job_id | num_midstates | starting_nonce | nbits |
          ntime | merkle_root[32] | prev_block_hash[32] | version |
          midstate[0..nm-1][32]
        Header type 0x21 = job write, single group; is_job=1 selects CRC16.
        Matches ESP-Miner BM1366_send_work exactly: NO chip_addr prefix, NO
        appended midstates (BM1366 computes midstate internally), num_midstates
        always 0x01, 4-byte fields LITTLE-endian (host memcpy = block-header order). */
     uint8_t data[1 + 1 + 4 + 4 + 4 + 32 + 32 + 4];  /* 82 bytes */
     uint16_t pos = 0;
     data[pos++] = job->job_id;
     data[pos++] = 0x01;                           /* num_midstates always 1 */
     memcpy(data + pos, &job->starting_nonce, 4); pos += 4;   /* LE */
     memcpy(data + pos, &job->nbits, 4);         pos += 4;
     memcpy(data + pos, &job->ntime, 4);         pos += 4;
     memcpy(data + pos, job->merkle_root, 32);   pos += 32;
     memcpy(data + pos, job->prev_block_hash, 32); pos += 32;
     memcpy(data + pos, &job->version, 4);       pos += 4;
     _send_packet(0x20 | 0x00 | 0x01, data, (uint8_t)pos, 1);
 }
 
 void bm1366_send_raw(const uint8_t *data, uint8_t len) {
     /* DIAG: dump sent bytes so we can confirm commands are going out. */
     printf("[CHIP] TX %u:", (unsigned)len);
     for (uint8_t i = 0; i < len; i++) printf(" %02X", data[i]);
     printf("\r\n");
     bm1366_uart_send(data, len);
 }
 
 int bm1366_read_result(bm1366_result_raw_t *result, uint32_t timeout_ms) {
     /* BM1366 nonce response is 11 bytes: AA 55 + 8 payload + CRC5.
        Reassembly state is static so a packet split across polls is not
        lost. timeout_ms=0 acts as a non-blocking poll: returns immediately
        if no byte is available and nothing is mid-assembly. */
     static uint8_t buf[11];
     static uint8_t idx = 0;
     uint32_t start = HAL_GetTick();

     for (;;) {
         uint8_t byte;
         if (!uart_rb_get(&byte)) {
             if (idx == 0) return 0;                       /* idle: non-blocking */
             if ((HAL_GetTick() - start) >= timeout_ms) return 0;  /* partial: give up */
             continue;
         }

         if (idx == 0) {
             if (byte == 0xAA) buf[idx++] = byte;          /* else keep scanning for preamble */
         } else if (idx == 1) {
             if (byte == 0x55) {
                 buf[idx++] = byte;
             } else {
                 buf[0] = byte;
                rx_bad_preamble++;                         /* DIAG */
                 idx = (byte == 0xAA) ? 1 : 0;
             }
         } else {
             buf[idx++] = byte;
             if (idx == 11) {
                 idx = 0;                                   /* reset for next packet */
                 /* CRC5 residual check: crc5 over 9 bytes (8 payload + crc byte)
                    must be 0. The old direct-compare (crc5(8)==buf[10]&0x1F)
                    rejected ~97% of valid nonces. */
                 if (bm1366_crc5(buf + 2, 9) == 0) {
                     memcpy(result, buf, 11);
                     return 1;
                 }
                 rx_bad_crc++;                              /* DIAG */
                /* CRC mismatch: drop and keep scanning */
             }
         }
     }
 }
 
 static uint16_t next_power_of_two(uint16_t v) {
     v--;
     v |= v >> 1;
     v |= v >> 2;
     v |= v >> 4;
     v |= v >> 8;
     return v + 1;
 }
 
 static uint8_t reverse_bits8(uint8_t num) {
     uint8_t reversed = 0;
     for (int i = 0; i < 8; i++) {
         reversed <<= 1;
         reversed |= num & 1;
         num >>= 1;
     }
     return reversed;
 }

 static void bm1366_get_difficulty_mask(uint16_t difficulty, uint8_t mask[6]) {
     /* Ported from ESP-Miner get_difficulty_mask: mask = (1<<floor(log2(diff)))-1,
        then each byte bit-reversed. For diff 256 -> {00 14 00 00 00 FF}. */
     uint32_t diff_int = (uint32_t)difficulty;
     int power = 0;
     while (diff_int > 1) { diff_int >>= 1; power++; }
     uint32_t m = (1U << power) - 1;
     mask[0] = 0x00;
     mask[1] = 0x14;  /* TICKET_MASK register */
     mask[2] = reverse_bits8((m >> 24) & 0xFF);
     mask[3] = reverse_bits8((m >> 16) & 0xFF);
     mask[4] = reverse_bits8((m >>  8) & 0xFF);
     mask[5] = reverse_bits8( m        & 0xFF);
 }
 
 /* ===== PLL calculation (matches ESP-Miner pll_get_parameters) =====
  * The caller passes (target, 144, 235) -- 144/235 are the FB_DIVIDER bounds,
  * NOT the refdiv. refdiv is searched over {1,2}. postdiv1 > postdiv2.
  * freq = 25 * fb / (refdiv * pd1 * pd2). */
 bm1366_pll_params_t bm1366_pll_calc(float target_freq_mhz,
                                      uint8_t fb_min, uint8_t fb_max) {
     bm1366_pll_params_t best = {0};
     best.actual_freq = 1e9f;

     for (uint8_t fb = fb_min; fb <= fb_max; fb++) {
         for (uint8_t refdiv = 1; refdiv <= 2; refdiv++) {
             for (uint8_t pd1 = 1; pd1 <= 7; pd1++) {
                 for (uint8_t pd2 = 1; pd2 < pd1; pd2++) {  /* pd1 > pd2 */
                     float f = 25.0f * (float)fb / (float)((float)refdiv * (float)pd1 * (float)pd2);
                     if (fabsf(f - target_freq_mhz) < fabsf(best.actual_freq - target_freq_mhz)) {
                         best.fb_divider = fb;
                         best.refdiv     = refdiv;
                         best.postdiv1   = pd1;
                         best.postdiv2   = pd2;
                         best.actual_freq = f;
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
     double hcn_max  = (double)hcn_space * (double)BM1366_FREQ_MULT / (double)frequency * 0.015625;  /* TEMP: 0.5->1/64, HCN 2^23->2^17 ~= Bitaxe's 105K */
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
     uint8_t vdo_scale = ((uint32_t)fb * 25 / ref >= 2400) ? 0x50 : 0x40;  /* 25 = FREQ_MULT (was 50) */
     uint8_t postdiv   = (uint8_t)(((pd1 - 1) & 0x0F) << 4) | ((pd2 - 1) & 0x0F);
     uint8_t cmd[] = {0x00, 0x08, vdo_scale, fb, ref, postdiv};
     bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_ALL | BM1366_CMD_WRITE, cmd, 6);
     return p.actual_freq;
 }
 
 int bm1366_count_chips(uint8_t expected_count) {
     /* init3 already sent the chip-ID read -- do NOT re-send (would double the
        responses). Just drain the 11-byte replies. Response layout (matches
        ESP-Miner count_asic_chips):
          AA 55 | chip_id[2]=0x13,0x66 | core_num | addr | ... | crc5
        chip_id is at buf[2..3], NOT buf[4..5] (that was the bug). */
     uint16_t total_wait = 5000;   /* generous: ESP-Miner waits 1000ms per frame, no total cap */
     uint16_t found = 0;
     uint32_t start = HAL_GetTick();
     /* DIAG: capture received bytes to see if chips respond at all. */
     struct { uint8_t b; uint32_t t; } diag[256];
     uint16_t diag_len = 0;
     while ((HAL_GetTick() - start) < total_wait && found < expected_count) {
         uint8_t byte;
         if (!uart_rb_get(&byte)) continue;
         if (diag_len < (uint16_t)(sizeof(diag) / sizeof(diag[0]))) {
            diag[diag_len].b = byte;
            diag[diag_len].t = HAL_GetTick() - start;
            diag_len++;
        }
         if (byte != 0xAA) continue;              /* scan for preamble */
         uint8_t buf[11];
         buf[0] = 0xAA;
         uint8_t idx = 1;
         uint32_t inner = HAL_GetTick();
         while (idx < 11 && (HAL_GetTick() - inner) < 1000) {
             if (uart_rb_get(&buf[idx])) idx++;
         }
         if (idx < 11) continue;                  /* incomplete frame */
         if (buf[1] != 0x55) continue;            /* bad preamble */
         uint16_t chip_id = ((uint16_t)buf[2] << 8) | buf[3];
         if (chip_id != BM1366_CHIP_ID) continue;
         if (bm1366_crc5(buf + 2, 9) != 0) continue;  /* CRC5 residual */
         found++;
     }
     /* DIAG: dump received bytes and count, so we can tell whether chips
        responded at all and whether the data looks garbled (level-shifter
        issue) or valid. */
     printf("[CHIP] recv %u bytes:", (unsigned)diag_len);
     for (uint16_t i = 0; i < diag_len; i++) printf(" %02X@%lu", diag[i].b, (unsigned long)diag[i].t);
     printf("\r\n[CHIP] found=%u  FE/NE=%u ORE=%u\r\n", (unsigned)found,
           (unsigned)uart_rx_frame_errors, (unsigned)uart_rx_overruns);
     return found;
 }
 
 int bm1366_init_chips(uint8_t expected_count, float target_freq_mhz) {
     GPIO_InitTypeDef gpio;
     /* Enable GPIOB clock and configure RST pin */
     __HAL_RCC_GPIOB_CLK_ENABLE();
     gpio.Pin   = BM1366_RST_PIN;
     gpio.Mode  = GPIO_MODE_OUTPUT_PP;
     gpio.Pull  = GPIO_NOPULL;
     gpio.Speed = GPIO_SPEED_FREQ_LOW;
     HAL_GPIO_Init(BM1366_RST_PORT, &gpio);
     /* Assert reset (active-low RESET_N) then release, to ensure a clean chip
        reset. Power-on reset alone may not be enough if the MCU reboots
        without power-cycling the chips. */
     printf("[CHIP] reset pulse (RST low 10ms -> high 50ms)\r\n");
     HAL_GPIO_WritePin(BM1366_RST_PORT, BM1366_RST_PIN, GPIO_PIN_RESET);
     HAL_Delay(100);
     HAL_GPIO_WritePin(BM1366_RST_PORT, BM1366_RST_PIN, GPIO_PIN_SET);
     HAL_Delay(100);
 
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

     /* Switch to 1 Mbaud (matches ESP-Miner: write reg 0x28 then switch UART) */
     {
         uint8_t baud_cmd[] = {0x00, 0x28, 0x11, 0x30, 0x02, 0x00};
         /* bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_ALL | BM1366_CMD_WRITE, baud_cmd, 6); TEMP: 1Mbaud disabled */
         /* TEMP: 1 Mbaud switch DISABLED (A/B test -- stay at 115200) */
    /* bm1366_uart_set_baud(BM1366_MAX_BAUD); */
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
uint32_t bm1366_get_bad_preamble(void) { return rx_bad_preamble; }
uint32_t bm1366_get_bad_crc(void) { return rx_bad_crc; }
uint32_t bm1366_get_rx_total(void) { return uart_rx_total; }
 
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
         uint32_t start = HAL_GetTick();
         while ((HAL_GetTick() - start) < step_delay_ms);
     }
     bm1366_set_frequency(target_freq_mhz);
 }
 
 void bm1366_set_voltage(uint8_t vdo_scale) {
     uint8_t cmd[] = {0x00, 0x08, vdo_scale, 0, 0, 0};
     bm1366_send_cmd(BM1366_TYPE_CMD | BM1366_GROUP_ALL | BM1366_CMD_WRITE, cmd, 6);
 }
