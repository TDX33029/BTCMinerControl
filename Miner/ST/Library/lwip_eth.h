/**
 * STM32F107 以太网寄存器级驱动头文件
 *
 * 精简替代 stm32f10x_eth.h, 无外部依赖。
 */

#ifndef __LWIP_ETH_H__
#define __LWIP_ETH_H__

#include "stm32f10x.h"
#include <stdint.h>

/* ===== DMA 描述符 ===== */
typedef struct {
    __IO uint32_t Status;
    __IO uint32_t ControlBufferSize;
    __IO uint32_t Buffer1Addr;
    __IO uint32_t Buffer2NextDescAddr;
    __IO uint32_t ExtendedStatus;
    __IO uint32_t Reserved;
    __IO uint32_t TimeStampLow;
    __IO uint32_t TimeStampHigh;
} ETH_DMADESCTypeDef;

/* DMA Tx 描述符 Status 位 */
#define ETH_DMATxDesc_OWN  ((uint32_t)0x80000000)
#define ETH_DMATxDesc_IC   ((uint32_t)0x40000000)
#define ETH_DMATxDesc_LS   ((uint32_t)0x20000000)
#define ETH_DMATxDesc_FS   ((uint32_t)0x10000000)
#define ETH_DMATxDesc_TCH  ((uint32_t)0x01000000)

/* DMA 中断位 */
#define ETH_DMA_IT_R       ((uint32_t)0x00000006)
#define ETH_DMA_IT_NIS     ((uint32_t)0x00010000)

/* ===== 公开函数 ===== */
uint16_t ETH_ReadPHYRegister(uint16_t phy, uint16_t reg);
void     ETH_WritePHYRegister(uint16_t phy, uint16_t reg, uint16_t val);
void     ETH_SoftwareReset(void);
uint32_t ETH_GetSoftwareResetStatus(void);
void     ETH_MAC_Init(uint8_t *mac_addr);
void     ETH_DMA_Init(ETH_DMADESCTypeDef *rx_desc, uint8_t *rx_buf, uint32_t rx_count,
                      ETH_DMADESCTypeDef *tx_desc, uint8_t *tx_buf, uint32_t tx_count,
                      uint32_t rx_buf_size, uint32_t tx_buf_size);
uint32_t ETH_GetRxPktSize(ETH_DMADESCTypeDef *rx_desc);
void     ETH_ReleaseRxDesc(ETH_DMADESCTypeDef *rx_desc, uint8_t *rx_buf,
                           uint32_t rx_count, uint32_t rx_buf_size);
void     ETH_DMATransmissionRequest(void);
void     ETH_DMAClearITPendingBit(uint32_t bit);

#endif /* __LWIP_ETH_H__ */
