/**
 * STM32F107 以太网 MAC + DMA 寄存器级驱动 (精简版)
 *
 * 替代 ST 标准外设库 stm32f10x_eth.c，减少外部依赖。
 * 仅实现本项目需要的: 软件复位, MAC 初始化, PHY SMI 读写, DMA 描述符管理。
 */

#include "stm32f10x.h"

/* ETH 寄存器基址 */
#define ETH_BASE_ADDR  0x40028000UL
#define ETH_REG(off)   (*(volatile uint32_t *)(ETH_BASE_ADDR + (off)))

/* ===== DMA 寄存器偏移 ===== */
#define DMABMR    0x1000   /* DMA bus mode */
#define DMATPDR   0x1004   /* TX poll demand */
#define DMARPDR   0x1008   /* RX poll demand */
#define DMARDLAR  0x100C   /* RX desc list addr */
#define DMATDLAR  0x1010   /* TX desc list addr */
#define DMASR     0x1014   /* DMA status */
#define DMAOMR    0x1018   /* DMA operation mode */
#define DMAIER    0x1024   /* DMA interrupt enable */

/* ===== MAC 寄存器偏移 ===== */
#define MACCR     0x0000
#define MACFFR    0x0004
#define MACHTHR   0x0008
#define MACHTLR   0x000C
#define MACMIIAR  0x0010
#define MACMIIDR  0x0014
#define MACFCR    0x0018
#define MACA0HR   0x0040
#define MACA0LR   0x0044

/* ===== MACCR 位 ===== */
#define MACCR_RE    (1UL << 2)
#define MACCR_TE    (1UL << 3)
#define MACCR_DM    (1UL << 4)
#define MACCR_FES   (1UL << 14)
#define MACCR_ROD   (1UL << 17)
#define MACCR_IPCO  (1UL << 18)
#define MACCR_APCS  (1UL << 23)
#define MACCR_RD    (1UL << 24)

/* ===== MACMIIAR 位 ===== */
#define MACMIIAR_MB  (1UL << 0)
#define MACMIIAR_MW  (1UL << 1)
#define MACMIIAR_CR  (0UL << 2)  /* Div42 (HCLK 60-100MHz) */

/* ===== DMABMR 位 ===== */
#define DMABMR_SR   (1UL << 0)   /* Software reset */

/* ===== DMAOMR 位 ===== */
#define DMAOMR_SR   (1UL << 1)   /* Start RX */
#define DMAOMR_ST   (1UL << 13)  /* Start TX */
#define DMAOMR_TSF  (1UL << 21)  /* Transmit store-and-forward */

/* ===== DMASR 位 ===== */
#define DMASR_RS    (1UL << 6)
#define DMASR_NIS   (1UL << 16)

/* ===== 公开函数 ===== */
#include "lwip_eth.h"

static volatile uint32_t * const eth = (volatile uint32_t *)ETH_BASE_ADDR;

/* PHY SMI 读 */
uint16_t ETH_ReadPHYRegister(uint16_t phy, uint16_t reg) {
    eth[MACMIIAR / 4] = MACMIIAR_CR | ((uint32_t)phy << 11) | ((uint32_t)reg << 6) | MACMIIAR_MB;
    volatile uint32_t t = 0xFFFFF; while ((eth[MACMIIAR / 4] & MACMIIAR_MB) && --t);
    return (uint16_t)(eth[MACMIIDR / 4] & 0xFFFF);
}

/* PHY SMI 写 */
void ETH_WritePHYRegister(uint16_t phy, uint16_t reg, uint16_t val) {
    eth[MACMIIDR / 4] = val;
    eth[MACMIIAR / 4] = MACMIIAR_CR | ((uint32_t)phy << 11) | ((uint32_t)reg << 6) | MACMIIAR_MW | MACMIIAR_MB;
    volatile uint32_t t = 0xFFFFF; while ((eth[MACMIIAR / 4] & MACMIIAR_MB) && --t);
}

/* 软件复位 MAC + DMA */
void ETH_SoftwareReset(void) {
    eth[DMABMR / 4] |= DMABMR_SR;
}

uint32_t ETH_GetSoftwareResetStatus(void) {
    return eth[DMABMR / 4] & DMABMR_SR;
}

/* MAC 初始化 (100M 全双工, 校验和卸载) */
void ETH_MAC_Init(uint8_t *mac_addr) {
    eth[MACCR / 4] = MACCR_RE | MACCR_TE | MACCR_DM | MACCR_FES
                   | MACCR_ROD | MACCR_IPCO | MACCR_APCS | MACCR_RD;
    eth[MACFFR / 4] = 0x00000400  /* hash/perfect filter */
                      | 0x00000020  /* broadcast */
                      | 0x00010000  /* pass all multicast */
                      | 0x80000000  /* receive all (temporarily) */
                      | 0x00000040; /* source address filter */

    /* MAC 地址 */
    eth[MACA0HR / 4] = ((uint32_t)mac_addr[4] << 8) | mac_addr[5];
    eth[MACA0LR / 4] = ((uint32_t)mac_addr[0] << 24) | ((uint32_t)mac_addr[1] << 16)
                     | ((uint32_t)mac_addr[2] << 8) | mac_addr[3];
}

/* DMA 初始化 */
void ETH_DMA_Init(ETH_DMADESCTypeDef *rx_desc, uint8_t *rx_buf, uint32_t rx_count,
                  ETH_DMADESCTypeDef *tx_desc, uint8_t *tx_buf, uint32_t tx_count,
                  uint32_t rx_buf_size, uint32_t tx_buf_size) {
    /* 总线模式: PBL=32 */
    eth[DMABMR / 4] = 0x00000100;

    /* 操作模式 */
    eth[DMAOMR / 4] = DMAOMR_SR | DMAOMR_TSF | DMAOMR_ST | (0UL << 14); /* RX:TX = 2:1 priority */

    /* 关中断 (轮询模式) */
    eth[DMAIER / 4] = 0;

    /* 清状态 */
    eth[DMASR / 4] = 0xFFFFFFFF;

    /* 初始化 RX 描述符 */
    for (uint32_t i = 0; i < rx_count; i++) {
        rx_desc[i].Status  = ETH_DMATxDesc_OWN;
        rx_desc[i].ControlBufferSize = rx_buf_size;
        rx_desc[i].Buffer1Addr = (uint32_t)(rx_buf + i * rx_buf_size);
        rx_desc[i].Buffer2NextDescAddr = (uint32_t)(uintptr_t)&rx_desc[(i + 1) % rx_count];
    }
    eth[DMARDLAR / 4] = (uint32_t)(uintptr_t)rx_desc;

    /* 初始化 TX 描述符 */
    for (uint32_t i = 0; i < tx_count; i++) {
        tx_desc[i].Status = 0;
        tx_desc[i].ControlBufferSize = 0;
        tx_desc[i].Buffer1Addr = (uint32_t)(tx_buf + i * tx_buf_size);
        tx_desc[i].Buffer2NextDescAddr = (uint32_t)(uintptr_t)&tx_desc[(i + 1) % tx_count];
    }
    eth[DMATDLAR / 4] = (uint32_t)(uintptr_t)tx_desc;
}

/* 获取接收帧长度 */
uint32_t ETH_GetRxPktSize(ETH_DMADESCTypeDef *rx_desc) {
    if (rx_desc[0].Status & ETH_DMATxDesc_OWN) return 0;
    return (rx_desc[0].Status & 0x3FFF0000) >> 16;
}

/* 释放 RX 描述符 */
void ETH_ReleaseRxDesc(ETH_DMADESCTypeDef *rx_desc, uint8_t *rx_buf, uint32_t rx_count, uint32_t rx_buf_size) {
    for (uint32_t i = 0; i < rx_count; i++) {
        rx_desc[i].Status  = ETH_DMATxDesc_OWN;
        rx_desc[i].ControlBufferSize = rx_buf_size;
    }
    /* 轮询 DMA 确保 RX 重启 */
    if ((eth[DMAOMR / 4] & DMAOMR_SR) == 0) {
        eth[DMAOMR / 4] |= DMAOMR_SR;
    }
    (void)rx_buf;
    (void)rx_count;
}

/* 触发 DMA 发送 */
void ETH_DMATransmissionRequest(void) {
    eth[DMATPDR / 4] = 0;
}
