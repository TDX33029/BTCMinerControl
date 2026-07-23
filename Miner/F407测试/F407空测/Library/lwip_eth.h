#ifndef __LWIP_ETH_H__
#define __LWIP_ETH_H__

#if defined(STM32F40_41xxx)
#include "stm32f4xx.h"
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
#include "stm32f10x.h"
#else
#include "stm32f4xx.h"
#endif
#include <stdint.h>

/* ===== DMA Descriptor ===== */
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

/* DMA TX descriptor Status bits */
#define ETH_DMATxDesc_OWN  ((uint32_t)0x80000000)
#define ETH_DMATxDesc_IC   ((uint32_t)0x40000000)
#define ETH_DMATxDesc_LS   ((uint32_t)0x20000000)
#define ETH_DMATxDesc_FS   ((uint32_t)0x10000000)
#define ETH_DMATxDesc_TCH  ((uint32_t)0x01000000)

#define ETH_DMA_IT_R       ((uint32_t)0x00000006)
#define ETH_DMA_IT_NIS     ((uint32_t)0x00010000)

/* ===== ETH Register Bit Definitions (F4 StdPeriph doesn't have these) ===== */
#ifndef ETH_MACA0HR_MO
#define ETH_MACA0HR_MO           ((uint32_t)0x80000000U)
#endif
#ifndef ETH_MACCR_IPCO
#define ETH_MACCR_IPCO           ((uint32_t)0x00000000U)
#endif
#ifndef ETH_MACCR_IFG_96
#define ETH_MACCR_IFG_96         ((uint32_t)0x00000000U)
#endif
#ifndef ETH_MACCR_DM
#define ETH_MACCR_DM             ((uint32_t)0x00000000U)
#endif
#ifndef ETH_MACCR_FES_100
#define ETH_MACCR_FES_100        ((uint32_t)0x00004000U)
#endif
#ifndef ETH_MACCR_CSD
#define ETH_MACCR_CSD            ((uint32_t)0x00000000U)
#endif
#ifndef ETH_MACCR_TE
#define ETH_MACCR_TE             ((uint32_t)0x00000008U)
#endif
#ifndef ETH_MACCR_RE
#define ETH_MACCR_RE             ((uint32_t)0x00000004U)
#endif
#ifndef ETH_MACFFR_HPF
#define ETH_MACFFR_HPF           ((uint32_t)0x00000004U)
#endif
#ifndef ETH_MACFFR_RA
#define ETH_MACFFR_RA            ((uint32_t)0x80000000U)
#endif
#ifndef ETH_MACMIIAR_MB
#define ETH_MACMIIAR_MB          ((uint32_t)0x10000000U)
#endif
#ifndef ETH_MACMIIDR_MD
#define ETH_MACMIIDR_MD          ((uint32_t)0x0000FFFFU)
#endif
#ifndef ETH_DMABMR_SR
#define ETH_DMABMR_SR            ((uint32_t)0x00000001U)
#endif
#ifndef ETH_DMABMR_AAB
#define ETH_DMABMR_AAB           ((uint32_t)0x02000000U)
#endif
#ifndef ETH_DMABMR_MB
#define ETH_DMABMR_MB            ((uint32_t)0x00000000U)
#endif
#ifndef ETH_DMABMR_RDP_32
#define ETH_DMABMR_RDP_32        ((uint32_t)0x00400000U)
#endif
#ifndef ETH_DMABMR_PBL_32
#define ETH_DMABMR_PBL_32        ((uint32_t)0x00002000U)
#endif
#ifndef ETH_DMABMR_FB
#define ETH_DMABMR_FB            ((uint32_t)0x00010000U)
#endif
#ifndef ETH_DMARxDesc_OWN
#define ETH_DMARxDesc_OWN        ((uint32_t)0x80000000U)
#endif
#ifndef ETH_DMARxDesc_RCH
#define ETH_DMARxDesc_RCH        ((uint32_t)0x00000000U)
#endif
#ifndef ETH_DMARxDesc_BSIZE
#define ETH_DMARxDesc_BSIZE      ((uint32_t)0x00003FFFU)
#endif
#ifndef ETH_DMARxDesc_LS
#define ETH_DMARxDesc_LS         ((uint32_t)0x10000000U)
#endif
#ifndef ETH_DMARxDesc_ES
#define ETH_DMARxDesc_ES         ((uint32_t)0x00008000U)
#endif
#ifndef ETH_DMARxDesc_FrameLength
#define ETH_DMARxDesc_FrameLength ((uint32_t)0x0FFF0000U)
#endif
#ifndef ETH_DMAOMR_OSF
#define ETH_DMAOMR_OSF           ((uint32_t)0x00000000U)
#endif
#ifndef ETH_DMAOMR_DTCEFD
#define ETH_DMAOMR_DTCEFD        ((uint32_t)0x00000000U)
#endif
#ifndef ETH_DMAOMR_TSF
#define ETH_DMAOMR_TSF           ((uint32_t)0x00200000U)
#endif
#ifndef ETH_DMAOMR_FTF
#define ETH_DMAOMR_FTF           ((uint32_t)0x00100000U)
#endif
#ifndef ETH_DMAOMR_TTC_64
#define ETH_DMAOMR_TTC_64        ((uint32_t)0x00000000U)
#endif
#ifndef ETH_DMAOMR_ST
#define ETH_DMAOMR_ST            ((uint32_t)0x00002000U)
#endif
#ifndef ETH_DMAOMR_SR
#define ETH_DMAOMR_SR            ((uint32_t)0x00000001U)
#endif
#ifndef ETH_DMASR_TPS
#define ETH_DMASR_TPS            ((uint32_t)0x00000000U)
#endif

/* ===== Public Functions ===== */
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
