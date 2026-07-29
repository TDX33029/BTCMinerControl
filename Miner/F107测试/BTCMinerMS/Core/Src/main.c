/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bm1366.h"
#include "protocol.h"
#include "eth_drv.h"
#include "eth_netif.h"
#include "lwip_eth.h"
#include "debug_serial.h"
#include "Delay.h"
#include "tps546d24a.h"
#include "tmp1075.h"
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define STATUS_LED_PIN   GPIO_PIN_13
#define STATUS_LED_PORT  GPIOC
#define ASIC_BOOT_PIN    GPIO_PIN_1
#define ASIC_BOOT_PORT   GPIOB
#define ASIC_CTRL_PIN    GPIO_PIN_14
#define ASIC_CTRL_PORT   GPIOB

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ETH_HandleTypeDef heth;

I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* ===== Application Config (edit to match your network) ===== */
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
#define PC_IP1     8
#define PC_IP2     1
#define PC_IP3     11
#define PC_PORT    4200

#define BM1366_EXPECTED_COUNT  1
#define BM1366_TARGET_FREQ_MHZ 485.0f
/* Bench-test switch: allow a received PC job to reach USART1 even when the
   chip probe finds no BM1366. Disable this on production firmware if desired. */
#define BM1366_UART_TEST_WITHOUT_ASIC  1

#define FW_VERSION_MAJOR  2
#define FW_VERSION_MINOR  4
#define FW_VERSION        ((FW_VERSION_MAJOR << 8) | FW_VERSION_MINOR)
/* Board ID: read at runtime from the STM32F107 96-bit unique device ID (UID)
   at 0x1FFFF7E8. We use the low 64 bits (UID words 0 and 1). */

/* ===== Global Variables ===== */
volatile uint32_t g_ms = 0;
static int  asic_ready    = 0;
static int  connected     = 0;
static uint64_t BOARD_ID  = 0;   /* set from STM32F107 UID at runtime */
static uint32_t active_job_version = 0;  /* base version of current job (for nonce version-rolling) */

static const eth_config_t eth_cfg = {
    .mac = {CFG_MAC0, CFG_MAC1, CFG_MAC2, CFG_MAC3, CFG_MAC4, CFG_MAC5},
    .ip  = {CFG_IP0, CFG_IP1, CFG_IP2, CFG_IP3},
    .gateway = {CFG_GW0, CFG_GW1, CFG_GW2, CFG_GW3},
    .subnet  = {CFG_SN0, CFG_SN1, CFG_SN2, CFG_SN3},
    .local_port = CFG_LOCAL_PORT
};

static uint8_t pc_ip_arr[4] = {PC_IP0, PC_IP1, PC_IP2, PC_IP3};
static uint8_t net_rx_buf[2048];
static uint16_t net_rx_buffered = 0;

static uint32_t last_reconnect = 0;
static uint32_t last_hello = 0;
static uint32_t last_sensor_probe = 0;
static uint32_t last_telemetry_sample = 0;
static tps546d24a_t tps546d24a;
static tmp1075_t tmp1075;
static uint8_t tps_detected = 0;
static uint8_t tmp_detected = 0;
static uint8_t tps_reported_state = 0xFFU;
static uint8_t tmp_reported_state = 0xFFU;
static uint8_t tps_desired_enabled = 1;
static protocol_telemetry_t board_telemetry;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ETH_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* Extra GPIO init for pins not handled by CubeMX */
static void MX_GPIO_Init_Ext(void);

/* Board hello / nonce send / TCP recv helpers */
static void send_board_hello(void);
static void send_board_telemetry(void);
static void probe_i2c_devices(void);
static void sample_i2c_telemetry(void);
static void check_bm1366_results(void);
static void receive_tcp_data(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* Early LED blink: 1 slow blink = clock init OK (before USART2 is ready) */
  {
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef early_led = {0};
    early_led.Pin = STATUS_LED_PIN;
    early_led.Mode = GPIO_MODE_OUTPUT_PP;
    early_led.Pull = GPIO_NOPULL;
    early_led.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(STATUS_LED_PORT, &early_led);
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_RESET);
    HAL_Delay(200);
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_SET);
    HAL_Delay(200);
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_RESET);
    HAL_Delay(200);
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_SET);
  }
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_GPIO_Init_Ext();
  /* ETH and I2C are initialized after the PHY diagnostics below. Calling
     MX_ETH_Init here can hang before diagnostics when REF_CLK is absent. */
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  /* ---- Quick diagnostics: USART2 should be alive now ---- */
  /* Raw byte test: write 'U' directly to USART2 DR (bypasses HAL) */
  USART2->DR = 'U';
  while (!(USART2->SR & USART_SR_TC)) {}
  USART2->DR = '\r';
  while (!(USART2->SR & USART_SR_TC)) {}
  USART2->DR = '\n';
  while (!(USART2->SR & USART_SR_TC)) {}

  printf("\r\n===== USART2 OK - DIAG START =====\r\n");

  /* Clock diagnostics */
  printf("[DIAG] HSE_VALUE=%lu Hz\r\n", (unsigned long)HSE_VALUE);
  printf("[DIAG] SystemCoreClock=%lu Hz\r\n", (unsigned long)SystemCoreClock);
  printf("[DIAG] PCLK1=%lu Hz (USART2 clk)\r\n", (unsigned long)HAL_RCC_GetPCLK1Freq());
  printf("[DIAG] PCLK2=%lu Hz (USART1 clk)\r\n", (unsigned long)HAL_RCC_GetPCLK2Freq());
  printf("[DIAG] huart2.gState=%d (expect 0x20=READY)\r\n", (int)huart2.gState);

  /* Board ID = STM32F107 96-bit unique device ID (UID @ 0x1FFFF7E8), low 64 bits */
  BOARD_ID = ((uint64_t)(*(uint32_t*)(UID_BASE + 4U)) << 32) | *(uint32_t*)UID_BASE;
  printf("[DIAG] Board ID (UID): 0x%08lX%08lX\r\n",
         (unsigned long)(BOARD_ID >> 32), (unsigned long)BOARD_ID);

  /* MCO/PLL3 check: PA8 (MCO) should output 50 MHz from PLL3 -> DP83848.
     If PLL3 isn't locked or MCO source isn't PLL3, PA8 is dead -> no clock
     to PHY/MAC -> MX_ETH_Init hangs on the DMA SWR (link stays DOWN too). */
  {
    uint32_t to = 200000;
    while (!__HAL_RCC_GET_FLAG(RCC_FLAG_PLLI2SRDY) && --to);
    int p3 = __HAL_RCC_GET_FLAG(RCC_FLAG_PLLI2SRDY) ? 1 : 0;
    printf("[CLK] PLL3(PLLI2S)ready=%d  RCC_CR=0x%08lX  RCC_CFGR=0x%08lX\r\n",
           p3, (unsigned long)RCC->CR, (unsigned long)RCC->CFGR);
  }

  /* STATUS LED blink: 3 fast blinks = USART init done */
  for (int i = 0; i < 3; i++) {
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_RESET);
    HAL_Delay(80);
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_SET);
    HAL_Delay(80);
  }

  /* ---- Probe PHY via MDIO BEFORE HAL_ETH_Init ----
     On this board PD2 no longer resets the DP83848 (the PHY resets with the
     MCU), so toggling PD2 is a no-op. HAL_ETH_Init's first step is a DMA
     software reset (DMABMR.SWR) that only self-clears while the 50 MHz
     REF_CLK (PHY -> PA1) is present; if REF_CLK is absent the SWR times out
     (HAL_TIMEOUT) and MX_ETH_Init -> Error_Handler hangs. MDIO (MDC/MDIO)
     does NOT need REF_CLK -- only the ETH peripheral clock -- so we can talk
     to the PHY here, before the DMA-SWR gate, to see whether it's alive and
     what mode it's strapped into. */
  printf("[DIAG] MDIO probe of PHY @0x01...\r\n");
  {
    __HAL_RCC_ETH_CLK_ENABLE();
    GPIO_InitTypeDef gi = {0};
    gi.Mode = GPIO_MODE_AF_PP; gi.Speed = GPIO_SPEED_FREQ_HIGH;
    gi.Pin = GPIO_PIN_1;  HAL_GPIO_Init(GPIOC, &gi);   /* MDC  = PC1 */
    gi.Pin = GPIO_PIN_2;  HAL_GPIO_Init(GPIOA, &gi);   /* MDIO = PA2 */
    uint16_t bcr  = ETH_ReadPHYRegister(0x01, 0x00);
    uint16_t bsr  = ETH_ReadPHYRegister(0x01, 0x01);
    uint16_t id1  = ETH_ReadPHYRegister(0x01, 0x02);
    uint16_t id2  = ETH_ReadPHYRegister(0x01, 0x03);
    uint16_t phys = ETH_ReadPHYRegister(0x01, 0x10);
    uint16_t s18  = ETH_ReadPHYRegister(0x01, 0x18);
    uint16_t s19  = ETH_ReadPHYRegister(0x01, 0x19);
    printf("[PHYPROBE] BCR=0x%04X BSR=0x%04X ID=0x%04X:0x%04X (exp 0x2000:0x5C90)\r\n",
           bcr, bsr, id1, id2);
    printf("[PHYPROBE] PHYSTS(0x10)=0x%04X STRAP18=0x%04X STRAP19=0x%04X\r\n",
           phys, s18, s19);
    printf("[PHYPROBE] link=%s  (ID=0xFFFF/0x0000 -> PHY not responding; check 25MHz xtal, MDIO wiring, PHY addr)\r\n",
           (bsr & 0x0004) ? "UP" : "DOWN");
  }

  /* Give the PHY time after MCU reset to emit a stable REF_CLK on PA1 */
  HAL_Delay(300);

  /* ---- Now init ETH ---- */
  printf("[DIAG] About to init ETH...\r\n");
  MX_ETH_Init();
  printf("[DIAG] ETH HAL init done\r\n");

  printf("[DIAG] About to init I2C...\r\n");
  MX_I2C1_Init();
  printf("[DIAG] I2C init done\r\n");

  tps546d24a_init(&tps546d24a, &hi2c1);
  tmp1075_init(&tmp1075, &hi2c1);
  probe_i2c_devices();
  sample_i2c_telemetry();
  last_sensor_probe = HAL_GetTick();
  last_telemetry_sample = last_sensor_probe;

  /* ?? network banner */
  printf("[NET] MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
         eth_cfg.mac[0], eth_cfg.mac[1], eth_cfg.mac[2],
         eth_cfg.mac[3], eth_cfg.mac[4], eth_cfg.mac[5]);
  printf("[NET] IP: %d.%d.%d.%d  GW: %d.%d.%d.%d  SN: %d.%d.%d.%d\r\n",
         eth_cfg.ip[0], eth_cfg.ip[1], eth_cfg.ip[2], eth_cfg.ip[3],
         eth_cfg.gateway[0], eth_cfg.gateway[1], eth_cfg.gateway[2], eth_cfg.gateway[3],
         eth_cfg.subnet[0], eth_cfg.subnet[1], eth_cfg.subnet[2], eth_cfg.subnet[3]);
  printf("[NET] Server: %d.%d.%d.%d:%d\r\n", PC_IP0, PC_IP1, PC_IP2, PC_IP3, PC_PORT);

  /* SysTick for g_ms (HAL already started SysTick) */
  extern volatile uint32_t g_ms;
  g_ms = HAL_GetTick();

  printf("\r\n========================================\r\n");
  printf("  BTCMinerMS F107VCT6 + DP83848\r\n");
  printf("  FW v%d.%d\r\n", FW_VERSION_MAJOR, FW_VERSION_MINOR);
  printf("========================================\r\n");
  printf("[SYS] ETH init...\r\n");
  if (!eth_init(&eth_cfg)) {
    printf("[ERR] ETH init FAILED!\r\n");
    while (1) { HAL_Delay(300); }
  }
  printf("[SYS] ETH OK\r\n");
  eth_print_phy_status();

#if BM1366_EXPECTED_COUNT > 0
  printf("[SYS] BM1366 init...\r\n");
  bm1366_uart_init();
  HAL_Delay(500);
  int chips = bm1366_init_chips(BM1366_EXPECTED_COUNT, BM1366_TARGET_FREQ_MHZ);
  asic_ready = (chips > 0) ? chips : 0;
  printf("[SYS] BM1366: %d chip(s)\r\n", chips);
#else
  asic_ready = 0;
  printf("[SYS] BM1366 disabled (ETH test mode)\r\n");
#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  static uint32_t last_led_toggle = 0;
  static uint32_t last_status     = 0;
  (void)last_reconnect; (void)last_hello;   /* file-scope static */

  while (1) {
    uint32_t now = HAL_GetTick();


    /* Drive the local 'connected' flag off the real TCP state in eth_drv.
       eth_connect() returns 0 right after issuing the (async) connect, and
       the link can drop (tcp_client_err / recv p==NULL) without main ever
       seeing it -- so a separate flag desyncs and we never reconnect. */
    {
      int net_ok = eth_is_connected();
      if (net_ok && !connected) {
        connected = 1;
        net_rx_buffered = 0;
        printf("[NET] TCP OK\r\n");
        send_board_hello();
        send_board_telemetry();
      } else if (!net_ok && connected) {
        connected = 0;
        net_rx_buffered = 0;
        printf("[NET] TCP down, will retry\r\n");
      }
    }

    if (!connected && (now - last_reconnect) > 1000) {
      last_reconnect = now;
      printf("[NET] Connecting %d.%d.%d.%d:%d...\r\n",
             PC_IP0, PC_IP1, PC_IP2, PC_IP3, PC_PORT);
      eth_connect(pc_ip_arr, PC_PORT);
    }

    eth_poll();
    receive_tcp_data();
    check_bm1366_results();

    if ((!tps_detected || !tmp_detected) &&
        (now - last_sensor_probe) >= 10000U) {
      last_sensor_probe = now;
      probe_i2c_devices();
    }

    if ((now - last_telemetry_sample) >= 2000U) {
      last_telemetry_sample = now;
      sample_i2c_telemetry();
      if (connected) send_board_telemetry();
    }

  if (connected && (now - last_hello) > 20000) {   /* 20s keepalive hello (was 60s -- server closed on idle) */
      last_hello = now;
      send_board_hello();
  }

    /* ? 30s ???????? */
    if ((now - last_status) > 10000) {
        last_status = now;
        printf("\r\n[STATUS] up=%lus  eth=%s  bm1366=%s  chips=%d  link=%s  rx=%lu tx=%lu DMASR=0x%08lX\r\n",
               (unsigned long)(now / 1000),
               connected ? "CONN" : "WAIT",
               asic_ready ? "OK" : "OFF",
               asic_ready ? bm1366_get_chip_count() : 0,
               eth_link_status() ? "UP" : "DOWN",
               (unsigned long)eth_netif_get_rx_count(),
               (unsigned long)eth_netif_get_tx_count(),
               (unsigned long)ETH->DMASR);
        if ((now % 30000) < 1000) eth_netif_reset_counts();  /* reset every 30s */
    }
    if ((now - last_led_toggle) > 500) {
        last_led_toggle = now;
        if (connected) {
            HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_RESET);
        } else {
            HAL_GPIO_TogglePin(STATUS_LED_PORT, STATUS_LED_PIN);
        }
    }

    /* Do not delay here. Ethernet RX is polled, so even a 1 ms sleep adds
       directly to ICMP and host-to-board command latency. */
  }  /* while(1) */

  /* Prevent watchdog-like reset */;
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV5;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.Prediv1Source = RCC_PREDIV1_SOURCE_PLL2;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  RCC_OscInitStruct.PLL2.PLL2State = RCC_PLL2_ON;
  RCC_OscInitStruct.PLL2.PLL2MUL = RCC_PLL2_MUL8;
  RCC_OscInitStruct.PLL2.HSEPrediv2Value = RCC_HSE_PREDIV2_DIV5;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_RCC_MCOConfig(RCC_MCO, RCC_MCO1SOURCE_PLL3CLK, RCC_MCODIV_1);

  /** Configure the Systick interrupt time
  */
  __HAL_RCC_HSE_PREDIV2_CONFIG(RCC_HSE_PREDIV2_DIV5);

  /** Configure the Systick interrupt time
  */
  __HAL_RCC_PLLI2S_CONFIG(RCC_PLLI2S_MUL10);

  /** Configure the Systick interrupt time
  */
  __HAL_RCC_PLLI2S_ENABLE();
}

/**
  * @brief ETH Initialization Function
  * @param None
  * @retval None
  */
static void MX_ETH_Init(void)
{

  /* USER CODE BEGIN ETH_Init 0 */

  /* USER CODE END ETH_Init 0 */

   static uint8_t MACAddr[6];

  /* USER CODE BEGIN ETH_Init 1 */

  /* USER CODE END ETH_Init 1 */
  heth.Instance = ETH;
  heth.Init.AutoNegotiation = ETH_AUTONEGOTIATION_ENABLE;
  heth.Init.Speed = ETH_SPEED_100M;
  heth.Init.DuplexMode = ETH_MODE_FULLDUPLEX;
  heth.Init.PhyAddress = LAN8742A_PHY_ADDRESS;
  MACAddr[0] = 0x00;
  MACAddr[1] = 0x80;
  MACAddr[2] = 0xE1;
  MACAddr[3] = 0x00;
  MACAddr[4] = 0x00;
  MACAddr[5] = 0x00;
  heth.Init.MACAddr = &MACAddr[0];
  heth.Init.RxMode = ETH_RXPOLLING_MODE;
  heth.Init.ChecksumMode = ETH_CHECKSUM_BY_HARDWARE;
  heth.Init.MediaInterface = ETH_MEDIA_INTERFACE_RMII;

  /* USER CODE BEGIN MACADDRESS */

  /* USER CODE END MACADDRESS */

  if (HAL_ETH_Init(&heth) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ETH_Init 2 */

  /* USER CODE END ETH_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15
                          |GPIO_PIN_2, GPIO_PIN_RESET);

  /*Configure GPIO pin : PB0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PD12 PD13 PD14 PD15
                           PD2 */
  GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15
                          |GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : PA8 */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* ===== ?? GPIO ??? (CubeMX ??????) ===== */
static void MX_GPIO_Init_Ext(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIO ???? MX_GPIO_Init ??? */

    /* PC13 - STATUS LED (????) */
    GPIO_InitStruct.Pin = STATUS_LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(STATUS_LED_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_SET);  /* ??: ? */

    /* PB1 - ASIC_BOOT (??) */
    GPIO_InitStruct.Pin = ASIC_BOOT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ASIC_BOOT_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(ASIC_BOOT_PORT, ASIC_BOOT_PIN, GPIO_PIN_RESET);

    /* PB14 - ASIC_CTRL (??) */
    GPIO_InitStruct.Pin = ASIC_CTRL_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ASIC_CTRL_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(ASIC_CTRL_PORT, ASIC_CTRL_PIN, GPIO_PIN_RESET);
}

static void send_board_hello(void) {
    protocol_hello_t hello = {
        .board_id = BOARD_ID,
        .fw_version = FW_VERSION,
        .asic_count = (uint8_t)asic_ready,
        .status = 0
    };
    uint8_t buf[64];
    uint16_t len = protocol_encode_hello(&hello, buf);
    if (len > 0) {
        printf("[HELLO] board_id=0x%08X%08X asic=%d fw=%d.%d len=%d\r\n",
               (uint32_t)(BOARD_ID >> 32), (uint32_t)BOARD_ID,
               hello.asic_count, FW_VERSION_MAJOR, FW_VERSION_MINOR, len);
        eth_send(buf, len);
    }
}

static void probe_i2c_devices(void) {
    if (!tps_detected) {
        tps_detected = tps546d24a_probe(&tps546d24a);
        if (tps_detected &&
            !tps546d24a_set_enabled(&tps546d24a, tps_desired_enabled)) {
            printf("[I2C] TPS546D24A default power command failed\r\n");
        }
    }
    if (!tmp_detected) tmp_detected = tmp1075_probe(&tmp1075);
    if (tps_reported_state != tps_detected) {
        printf("[I2C] TPS546D24A @0x%02X: %s\r\n",
               TPS546D24A_I2C_ADDRESS,
               tps_detected ? "detected" : "not detected");
        tps_reported_state = tps_detected;
    }
    if (tmp_reported_state != tmp_detected) {
        if (tmp_detected) {
            printf("[I2C] TMP1075 @0x%02X: detected\r\n", tmp1075.address);
        } else {
            printf("[I2C] TMP1075 @0x48-0x4F: not detected\r\n");
        }
        tmp_reported_state = tmp_detected;
    }
}

static void sample_i2c_telemetry(void) {
    tps546d24a_telemetry_t tps_data;
    int16_t tmp_temperature = 0;
    uint8_t tps_valid = 0;
    uint8_t tmp_valid = 0;

    memset(&board_telemetry, 0, sizeof(board_telemetry));
    if (tps_detected) {
        tps_valid = tps546d24a_read_telemetry(&tps546d24a, &tps_data);
        if (tps_valid) {
            board_telemetry.vout_mv = tps_data.vout_mv;
            board_telemetry.iout_ma = tps_data.iout_ma;
            board_telemetry.power_mw = tps_data.power_mw;
            board_telemetry.tps_temperature_centi_c = tps_data.temperature_centi_c;
            board_telemetry.tps_status_word = tps_data.status_word;
            board_telemetry.power_enabled = tps_data.enabled;
        } else if (!tps546d24a_probe(&tps546d24a)) {
            tps_detected = 0;
        }
    }

    if (tmp_detected) {
        tmp_valid = tmp1075_read_temperature(&tmp1075, &tmp_temperature);
        if (tmp_valid) {
            board_telemetry.tmp_temperature_centi_c = tmp_temperature;
        } else if (HAL_I2C_IsDeviceReady(&hi2c1,
                   (uint16_t)(tmp1075.address << 1), 1, 20) != HAL_OK) {
            tmp_detected = 0;
            tmp1075.address = 0;
        }
    }

    if (tps_detected) board_telemetry.flags |= TELEMETRY_FLAG_TPS_DETECTED;
    if (tmp_detected) board_telemetry.flags |= TELEMETRY_FLAG_TMP_DETECTED;
    if (tps_valid) board_telemetry.flags |= TELEMETRY_FLAG_TPS_VALID;
    if (tmp_valid) board_telemetry.flags |= TELEMETRY_FLAG_TMP_VALID;
    if (tps_valid) board_telemetry.flags |= TELEMETRY_FLAG_POWER_VALID;
    board_telemetry.tps_address = tps_detected ? tps546d24a.address : 0;
    board_telemetry.tmp_address = tmp_detected ? tmp1075.address : 0;
}

static void send_board_telemetry(void) {
    uint8_t buf[32];
    uint16_t len = protocol_encode_telemetry(&board_telemetry, buf);
    if (len > 0) eth_send(buf, len);
}

static void check_bm1366_results(void) {
    if (!asic_ready) return;
    bm1366_result_raw_t raw;
    if (bm1366_read_result(&raw, 0) > 0) {
        /* Field parsing matches ESP-Miner BM1366_process_work:
           - nonce on wire is big-endian -> byte-swap for host order
           - job_id_raw: high 5 bits = job_id, low 3 bits = small_core_id
           - core_id and asic_nr are extracted from the NONCE (not midstate_num)
           - version_raw is big-endian -> byte-swap, shift <<13, OR with base version */
        bm1366_result_t parsed = {0};
        uint32_t nonce_h = ((raw.nonce & 0xFFU) << 24) | ((raw.nonce & 0xFF00U) << 8) |
                            ((raw.nonce & 0xFF0000U) >> 8) | ((raw.nonce & 0xFF000000U) >> 24);
        uint16_t ver_h   = ((raw.version_raw & 0xFFU) << 8) | ((raw.version_raw >> 8) & 0xFFU);
        parsed.nonce        = nonce_h;
        parsed.job_id       = raw.job_id_raw & 0xF8;
        parsed.small_core_id= raw.job_id_raw & 0x07;
        parsed.core_id      = (nonce_h >> 25) & 0x7F;
        parsed.asic_nr      = ((nonce_h >> 17) & 0xFF) / bm1366_get_address_interval();
        parsed.rolled_version = active_job_version | ((uint32_t)ver_h << 13);
        printf("[NONCE] job_id=%d nonce=0x%08X core=%d small=%d asic=%d\r\n",
               parsed.job_id, parsed.nonce, parsed.core_id, parsed.small_core_id, parsed.asic_nr);
        uint8_t buf[64];
        uint16_t len = protocol_encode_nonce(&parsed, BOARD_ID, buf);
        if (len > 0) eth_send(buf, len);
    }
}

static void receive_tcp_data(void) {
    int received = eth_recv(net_rx_buf + net_rx_buffered,
                            (uint16_t)(sizeof(net_rx_buf) - net_rx_buffered));
    if (received > 0) net_rx_buffered = (uint16_t)(net_rx_buffered + received);
    if (net_rx_buffered == 0) return;

    /* A single TCP segment may carry several protocol frames back-to-back,
       and they were already ACKed to lwIP inside eth_recv -- so we must walk
       the whole buffer, not just the first frame, or we silently lose the
       rest. The wire format is [4B big-endian total][1B type][payload],
       so the payload begins at offset 5 of each frame. */
    uint16_t off = 0;
    while (off + 4 <= net_rx_buffered) {
        uint16_t frame_len, payload_len;
        uint8_t type = protocol_peek_frame(net_rx_buf + off, net_rx_buffered - off,
                                           &frame_len, &payload_len);
        if (frame_len == 0xFFFFU) {
            printf("[TCP] Invalid frame length; dropping buffered stream\r\n");
            net_rx_buffered = 0;
            return;
        }
        if (type == 0) break;                                  /* incomplete: preserve it */

        const uint8_t *payload = net_rx_buf + off + 5;
        if (type != MSG_LATENCY_PROBE) {
            printf("[TCP] type=0x%02X frame=%d payload=%d\r\n", type, frame_len, payload_len);
        }

        switch (type) {
            case MSG_JOB: {
                protocol_job_t job;
                if (protocol_decode_job(payload, payload_len, &job) > 0) {
                    active_job_version = job.version;   /* track for nonce version-rolling */
                    printf("[JOB] id=%d midstates=%d version=0x%08X nbits=0x%08X ntime=0x%08X nonce_start=0x%08X\r\n",
                           job.job_id, job.num_midstates, job.version,
                           job.nbits, job.ntime, job.starting_nonce);
                    if (asic_ready || BM1366_UART_TEST_WITHOUT_ASIC) {
                        bm1366_send_job(&job);
                        if (!asic_ready) printf("[UART1-TEST] Job forwarded without ASIC\r\n");
                    }
                }
                break;
            }
            case MSG_SET_PARAMS: {
                protocol_setparams_t params;
                if (protocol_decode_setparams(payload, payload_len, &params) > 0) {
                    printf("[PARAM] freq=%d MHz voltage=%d mV\r\n",
                           params.freq_mhz, params.voltage_mv);
                    if (asic_ready) bm1366_frequency_transition((float)params.freq_mhz, 100);
                }
                break;
            }
            case MSG_SET_POWER: {
                protocol_setpower_t power;
                uint8_t reply[64];
                uint16_t reply_len;
                if (!protocol_decode_setpower(payload, payload_len, &power)) {
                    reply_len = protocol_encode_error(0x20, "invalid power command", reply);
                } else if (!tps_detected) {
                    reply_len = protocol_encode_error(0x21, "TPS546D24A not detected", reply);
                } else if (!tps546d24a_set_enabled(&tps546d24a, power.enabled)) {
                    reply_len = protocol_encode_error(0x22, "TPS546D24A power command failed", reply);
                } else {
                    tps_desired_enabled = power.enabled;
                    printf("[POWER] TPS546D24A %s\r\n", power.enabled ? "ON" : "OFF");
                    sample_i2c_telemetry();
                    send_board_telemetry();
                    reply_len = protocol_encode_ack(MSG_SET_POWER, reply);
                }
                if (reply_len > 0) eth_send(reply, reply_len);
                break;
            }
            case MSG_LATENCY_PROBE: {
                protocol_latency_t latency;
                uint8_t reply[16];
                if (protocol_decode_latency(payload, payload_len, &latency)) {
                    uint16_t reply_len = protocol_encode_latency(&latency, reply);
                    if (reply_len > 0) eth_send(reply, reply_len);
                }
                break;
            }
            default:
                printf("[TCP] Unknown type 0x%02X, hex:", type);
                for (uint16_t i = 0; i < (payload_len > 16 ? 16 : payload_len); i++)
                    printf(" %02X", payload[i]);
                printf("\r\n");
                break;
        }

        off += frame_len;
    }

    if (off > 0) {
        net_rx_buffered = (uint16_t)(net_rx_buffered - off);
        if (net_rx_buffered > 0) memmove(net_rx_buf, net_rx_buf + off, net_rx_buffered);
    } else if (net_rx_buffered == sizeof(net_rx_buf)) {
        printf("[TCP] Receive frame exceeds buffer; dropping stream\r\n");
        net_rx_buffered = 0;
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
