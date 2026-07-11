# STM32F107VCT6 + DP83848 + 6x BM1366 矿机桥接固件

## 编译状态

- **编译通过** — 0 Error(s), 0 Warning(s)
- **编译器** — Arm Compiler 6.24 (Keil MDK V5.43)
- **芯片** — STM32F107VCT6 (256KB Flash, 64KB SRAM, LQFP-100)
- **Flash 占用** — 13,996 bytes (5.3%)
- **RAM 占用** — 4,224 bytes (6.4%)
- **当前模式** — ETH_NO_LWIP (TCP stub, 需 LwIP 源码启用完整以太网)

## 引脚分配 (已验证 STM32F107 参考手册)

### 以太网 (RMII 默认引脚, 不重映射)
| 功能 | F107 引脚 | 方向 | 连接目标 |
|------|-----------|------|---------|
| ETH_MDC | PC1 | OUT | DP83848 MDC (pin 30) |
| ETH_MDIO | PA2 | I/O | DP83848 MDIO (pin 31, 需 1.5kΩ 上拉) |
| ETH_REF_CLK | PA1 | IN | DP83848 X1/REF_CLK (pin 34, 50MHz) |
| ETH_CRS_DV | PA7 | IN | DP83848 CRS_DV (pin 40) |
| ETH_RXD0 | PC4 | IN | DP83848 RXD_0 (pin 43) |
| ETH_RXD1 | PC5 | IN | DP83848 RXD_1 (pin 44) |
| ETH_TX_EN | PB11 | OUT | DP83848 TX_EN (pin 2) |
| ETH_TXD0 | PB12 | OUT | DP83848 TXD_0 (pin 3) |
| ETH_TXD1 | PB13 | OUT | DP83848 TXD_1 (pin 4) |
| ETH_RST | PD2 | OUT | DP83848 RESET_N (pin 29) |

### BM1366 (UART1)
| 功能 | F107 引脚 | 方向 | 说明 |
|------|-----------|------|------|
| ASIC_TX | PA9 | OUT | BM1366 RX (链首) |
| ASIC_RX | PA10 | IN | BM1366 TX |
| ASIC_RST | PB0 | OUT | BM1366 硬件复位 |
| ASIC_BOOT | PB1 | OUT | (可选) |
| ASIC_CTRL | PB14 | OUT | (可选) |

### LED
| 功能 | F107 引脚 | 说明 |
|------|-----------|------|
| STATUS | PC13 | 板载 LED (低电平亮) |
| LINK | PD12 | 以太网连接指示 |
| ACT | PD13 | 以太网活动指示 |

### 调试
| SWDIO | PA13 | 调试数据 |
| SWCLK | PA14 | 调试时钟 |

## 引脚冲突说明
- PA1: ETH_REF_CLK (RMII 输入时钟) — 不能用作 GPIO
- PA7: ETH_CRS_DV — 不能用作 GPIO (TIM3_CH2, SPI1_MOSI 等复用)
- PB11-PB13: ETH TX 信号 — 不能用作 GPIO
- PC1: ETH_MDC — 不能用作 GPIO
- PC4, PC5: ETH RX 信号 — 不能用作 GPIO
- PB14: BM1366 CTRL — 与 SPI2_MISO 复用, 无冲突
