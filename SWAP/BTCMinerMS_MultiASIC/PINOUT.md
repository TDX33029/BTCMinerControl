# STM32F107VCT6 引脚连线表 (导出自 BTCMinerMS.ioc + 固件源码核对)

- **芯片**: STM32F107VCT6, LQFP-100, 256KB Flash / 64KB SRAM
- **时钟**: HSE 25MHz 晶振 -> SYSCLK 72MHz; RMII 50MHz 参考时钟见以太网章节说明 (PA1/PA8)
- **来源**: `BTCMinerMS.ioc` (CubeMX 配置) + `main.c` / `bm1366.c` / `eth_drv.c` 中的用户 GPIO 定义
- **核对日期**: 2026-08-17

## 以太网 RMII (DP83848) - 默认引脚, 无重映射 (板卡接线已更新)

| 功能 | F107 引脚 | 方向 | 连接目标 | 备注 |
|------|-----------|------|---------|------|
| REF_CLK | PA1 | IN | 50MHz 时钟网络 (与 X1、MCO 共网) | MAC 参考时钟输入, 见下方拓扑说明 |
| PHY 时钟 | PA8 (MCO) | OUT | 50MHz 时钟网络 -> DP83848 X1 (pin 34) | PLL3 50MHz: 25MHz÷5×10 |
| ETH_MDIO | PA2 | I/O | DP83848 MDIO (pin 31) | 需 1.5kΩ 上拉 |
| ETH_MDC | PC1 | OUT | DP83848 MDC (pin 30) | |
| ETH_CRS_DV | **PA7** | IN | DP83848 CRS_DV (pin 40) | 默认引脚 (旧板曾用 PD8 重映射) |
| ETH_RXD0 | **PC4** | IN | DP83848 RXD_0 (pin 43) | 默认引脚 (旧板曾用 PD9 重映射) |
| ETH_RXD1 | **PC5** | IN | DP83848 RXD_1 (pin 44) | 默认引脚 (旧板曾用 PD10 重映射) |
| ETH_TX_EN | PB11 | OUT | DP83848 TX_EN (pin 2) | |
| ETH_TXD0 | PB12 | OUT | DP83848 TXD_0 (pin 3) | |
| ETH_TXD1 | PB13 | OUT | DP83848 TXD_1 (pin 4) | |
| PHY 复位 | PD2 | OUT | DP83848 RESET_N (pin 29) | `eth_drv.c` PHY 复位脉冲 |

> **50MHz 时钟拓扑 (已确认)**: PA8 (MCO)、PA1 (ETH_REF_CLK)、DP83848 X1 三点共网。
> F107 的 MCO 输出引脚只有 PA8; PA1 在 RMII 下是 MAC 参考时钟输入 (硅限制, 不能输出)。
> 时钟链: HSE 25MHz 晶振 -> PREDIV2÷5 -> 5MHz -> PLL3×10 -> 50MHz -> MCO (PA8) 输出,
> 同时驱动 PHY X1 与 PA1 (等效 25MHz×2 倍频; PLL 无 ×2 档, 用 ÷5×10 实现)。
> 固件 `main.c` SystemClock_Config + MCOConfig 已按此配置, 无需改动。
>
> **旧版差异 (2026-08-17 更新)**: 旧板使用 `AFIO_MAPR2 ETH_REMAP` 把
> CRS_DV/RXD0/RXD1 映射到 PD8/PD9/PD10。新板接线为默认引脚 PA7/PC4/PC5, 固件已
> 删除 `__HAL_AFIO_REMAP_ETH_ENABLE()` 调用, PD8/PD9/PD10 释放为空闲。

## BM1366 ASIC 链 (USART1, 115200-8N1)

| 功能 | F107 引脚 | 方向 | 连接目标 | 备注 |
|------|-----------|------|---------|------|
| ASIC_TX | PA9 (USART1_TX) | OUT | BM1366 链首 RX | |
| ASIC_RX | PA10 (USART1_RX) | IN | BM1366 链尾 TX | |
| ASIC_RST | PB0 | OUT | BM1366 RESET_N | 低电平复位, `bm1366.c` 控制 |
| ASIC_BOOT | PB1 | OUT | (可选) 预留 | `main.c` MX_GPIO_Init_Ext |
| ASIC_CTRL | PB14 | OUT | (可选) 预留 | 与 SPI2_MISO 复用无冲突 |

## I2C1 — 电源与温度传感器

| 功能 | F107 引脚 | 方向 | 连接目标 | 备注 |
|------|-----------|------|---------|------|
| I2C1_SCL | PB6 | OUT(OD) | TPS546D24A SCL + TMP1075 SCL | 共总线 |
| I2C1_SDA | PB7 | I/O(OD) | TPS546D24A SDA + TMP1075 SDA | 共总线 |

| 器件 | 7 位地址 | 用途 |
|------|---------|------|
| TPS546D24A | 0x24 | 核心 Vout 降压 (遥测电压/电流/功率, 输出使能) |
| TMP1075 | 0x48–0x4F | 板温传感器 (自动探测) |

## 调试串口 (USART2)

| 功能 | F107 引脚 | 方向 | 备注 |
|------|-----------|------|------|
| DBG_TX | PD5 (USART2_TX) | OUT | printf 日志输出 (`debug_serial.c`) |
| DBG_RX | PD6 (USART2_RX) | IN | 预留 |

## LED / 状态

| 功能 | F107 引脚 | 备注 |
|------|-----------|------|
| STATUS | PC13 | 板载 LED, 低电平点亮, 心跳翻转 |
| LED4 | PD12 | GPIO 输出, 初始化为低 (预留 LINK 指示) |
| LED5 | PD13 | GPIO 输出, 初始化为低 (预留 ACT 指示) |
| LED6 | PD14 | GPIO 输出, 初始化为低 (预留) |
| LED7 | PD15 | GPIO 输出, 初始化为低 (预留) |

## 显式高阻引脚

| 功能 | F107 引脚 | 配置 | 说明 |
|------|-----------|------|------|
| 高阻 | PA0 | 浮空输入 (INPUT + NOPULL) | `main.c` MX_GPIO_Init_Ext 显式配置; 兼作 WKUP/ADC12_IN0, 本设计均未用 |

## SWD 调试口

| 功能 | F107 引脚 |
|------|-----------|
| SWDIO | PA13 |
| SWCLK | PA14 |

## 时钟与晶振

| 功能 | F107 引脚 | 备注 |
|------|-----------|------|
| HSE 25MHz | OSC_IN / OSC_OUT | PREDIV2÷5 → 5MHz → PLL2×9 → 45MHz → PREDIV1÷5 → 9MHz → PLL×8 → 72MHz |
| LSE 32.768kHz | PC14 / PC15 | 未使用, 保持 OSC32 功能预留 |

## 引脚占用汇总 (LQFP-100 已用 29 脚)

```
PA1  ETH_REF_CLK(in)   PA9  USART1_TX        PB0  ASIC_RST        PC1  ETH_MDC
PA2  ETH_MDIO          PA10 USART1_RX        PB1  ASIC_BOOT       PC13 STATUS_LED
PA8  MCO 50MHz         PA13 SWDIO            PB6  I2C1_SCL        PC14 OSC32_IN(预留)
PA14 SWCLK                                   PB7  I2C1_SDA        PC15 OSC32_OUT(预留)
                                             PB11 ETH_TX_EN      PD2  PHY_RST
                                             PB12 ETH_TXD0       PD5  USART2_TX
                                             PB13 ETH_TXD1       PD6  USART2_RX
                                             PB14 ASIC_CTRL      PD12 LED4(预留)
                                                                 PD13 LED5(预留)
                                                                 PD14 LED6(预留)
                                                                 PD15 LED7(预留)
PA7  ETH_CRS_DV       PC4  ETH_RXD0           PC5  ETH_RXD1
(PD8/PD9/PD10 已释放空闲)
```

## 冲突与注意事项

- PA7/PC4/PC5 (RMII RX) 与 PC1 (MDC)、PA1 (REF_CLK)、PA2 (MDIO)、PB11/PB12/PB13 (TX) 不能用作其他功能
- PA7 与 SPI1_MOSI 复用, PC4/PC5 与 ADC12_IN14/IN15 复用: 本设计未用 SPI1/这两个 ADC 通道, 无冲突
- PD8/PD9/PD10 已随旧重映射方案一并释放, 空闲可用
- 50MHz 时钟由 PA8 MCO 提供 (PLL3, 固件已配置): PA8 必须与 DP83848 X1、PA1 连通,
  否则 PHY 无时钟、MAC 无参考时钟, 链路起不来 (`main.c` 有 MCO/PLL3 自检诊断日志)
- 更换 HSE 晶振频率需在 CubeMX 重新配置 PLL2/PREDIV/PLL 链, 保持 SYSCLK=72MHz 且 MCO=50MHz
