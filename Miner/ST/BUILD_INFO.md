# STM32F107VCT6 BM1366 矿机桥接 — 编译汇总

**Compiler**: Arm Compiler 6.24 (AC6, Keil MDK V5.43)
**Chip**:     STM32F107VCT6 (256KB Flash, 64KB SRAM, LQFP-100)
**Target**:   BTCMinerBridge_F107
**修正时间**: 2026-07-12
**Output**:   Objects/BTCMinerBridge_F107.hex (39KB)

## 编译结果
| 指标 | 值 |
|------|-----|
| **Errors**   | 0 |
| **Warnings** | 0 |
| **Flash占用** | 13,992 bytes (5.3% of 256KB) |
| **RAM占用**   | 4,224 bytes (6.4% of 64KB) |

## 内存明细
| 段 | 大小 | 说明 |
|----|------|------|
| Code (text) | 13,350 | 程序代码 |
| RO Data | 642 | 只读数据 (常量/字符串/CRC表) |
| RW Data | 4 | 已初始化全局变量 |
| ZI Data | 4,220 | 零初始化 (BSS + 堆栈) |

## 引脚分配 (已修正, 2026-07-12)
| 外设 | 引脚 | 功能 |
|------|------|------|
| ETH_RMII | PB11 (TX_EN), PB12 (TXD0), PB13 (TXD1), PC4 (RXD0), PC5 (RXD1), PA7 (CRS_DV), PA1 (REF_CLK), PC1 (MDC), PA2 (MDIO), PD2 (RST) | DP83848 PHY
| _✔ AFIO_MAPR MII_RMII_SEL set_ | _SW fix: RMII mode_ | _was MII default!_ |
| BM1366 | PA9 (TX), PA10 (RX), PB0 (RST), PB1 (BOOT), PB14 (CTRL) | ASIC UART 级联 |
| LED   | PC13 (STATUS), PD12 (LINK), PD13 (ACT) | 状态指示 |
| Debug | PA13 (SWDIO), PA14 (SWCLK) | SWD 调试 |

## 状态
- **ETH_NO_LWIP** 模式编译 (TCP stub)
- **注**: eth_drv.c 中 RMII/AHB/PREDIV2 等修正需移除 ETH_NO_LWIP 后才生效
- 注意: PB12/PB13 被 ETH_TXD0/TXD1 占用，BM1366 RST 改为 PB0
- 完整引脚请见: PINOUT.md
