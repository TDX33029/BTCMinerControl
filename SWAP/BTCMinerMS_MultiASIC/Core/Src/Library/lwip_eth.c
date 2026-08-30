 #include "lwip_eth.h"
 #include <stdio.h>

 /* ===== MAC loopback diagnostic =====
  * Set to 1 to turn on the MAC's internal loopback (LM): the MAC routes its
  * own TX back into its RX, bypassing the PHY / RMII pins entirely.
  *
  *   - If, in loopback, the device's rx counter climbs together with tx
  *     (it receives its own ARP requests)  -> the MAC TX/RX works, so the
  *       "frames don't reach the wire" problem is on the RMII TX hardware
  *       path: PB11(TX_EN)/PB12(TXD0)/PB13(TXD1) -> DP83848 pins 2/3/4,
  *       or the PHY's transmit side.
  *   - If rx stays flat in loopback               -> the MAC TX isn't really
  *       producing frames (software/config), and we keep digging.
  *
  * Leave 0 for normal operation. */
 #define MAC_LOOPBACK_TEST 0

 /* ===== SMI (MDC/MDIO) Access ===== */
uint16_t ETH_ReadPHYRegister(uint16_t phy, uint16_t reg) {
    uint32_t tmpreg;
    uint32_t timeout = 100000;

    /* Read current value, preserve everything except CR */
    tmpreg = ETH->MACMIIAR;
    tmpreg &= ~ETH_MACMIIAR_CR;                    /* clear CR bits 4:2 */
    tmpreg &= ~ETH_MACMIIAR_PA;                    /* clear PA bits 15:11 */
    tmpreg &= ~ETH_MACMIIAR_MR;                    /* clear MR bits 10:6  */
    tmpreg |= ETH_MACMIIAR_CR_Div102;              /* CR=Div102 for 168MHz */
    tmpreg |= ((uint32_t)phy << 11) & ETH_MACMIIAR_PA;
    tmpreg |= ((uint32_t)reg << 6) & ETH_MACMIIAR_MR;
    tmpreg &= ~ETH_MACMIIAR_MW;                    /* read mode */
    tmpreg |= ETH_MACMIIAR_MB;                     /* start transaction */
    ETH->MACMIIAR = tmpreg;

    /* Wait with timeout */
    while (ETH->MACMIIAR & ETH_MACMIIAR_MB) {
        if (!--timeout) {
            break;   /* MDIO timed out (PHY not responding). Do NOT DMA-reset
                      * here: DMABMR.SWR only self-clears while REF_CLK is
                      * present, so it would hang forever when the PHY itself
                      * is the thing not responding. */
        }
    }

    return (uint16_t)(ETH->MACMIIDR & ETH_MACMIIDR_MD);
}

void ETH_WritePHYRegister(uint16_t phy, uint16_t reg, uint16_t val) {
    uint32_t tmpreg;
    uint32_t timeout = 100000;

    /* Write data first (latched when MB is set) */
    ETH->MACMIIDR = (uint32_t)val;

    /* Read current value, preserve everything except CR */
    tmpreg = ETH->MACMIIAR;
    tmpreg &= ~ETH_MACMIIAR_CR;                    /* clear CR bits 4:2 */
    tmpreg &= ~ETH_MACMIIAR_PA;                    /* clear PA bits 15:11 */
    tmpreg &= ~ETH_MACMIIAR_MR;                    /* clear MR bits 10:6  */
    tmpreg |= ETH_MACMIIAR_CR_Div102;              /* CR=Div102 for 168MHz */
    tmpreg |= ((uint32_t)phy << 11) & ETH_MACMIIAR_PA;
    tmpreg |= ((uint32_t)reg << 6) & ETH_MACMIIAR_MR;
    tmpreg |= ETH_MACMIIAR_MW;                     /* write mode */
    tmpreg |= ETH_MACMIIAR_MB;                     /* start transaction */
    ETH->MACMIIAR = tmpreg;

    /* Wait with timeout */
    while (ETH->MACMIIAR & ETH_MACMIIAR_MB) {
        if (!--timeout) {
            break;   /* MDIO write timed out -- see ETH_ReadPHYRegister */
        }
    }
}

/* ===== MAC Initialization ===== */
 void ETH_SoftwareReset(void) {
     ETH->DMABMR |= ETH_DMABMR_SR;
 }
 
 uint32_t ETH_GetSoftwareResetStatus(void) {
     return (ETH->DMABMR & ETH_DMABMR_SR);
 }
 
 void ETH_MAC_Init(uint8_t *mac_addr) {
     uint32_t mac_hi = ((uint32_t)mac_addr[0] << 8) | (uint32_t)mac_addr[1];
     uint32_t mac_lo = ((uint32_t)mac_addr[2] << 24) | ((uint32_t)mac_addr[3] << 16) |
                       ((uint32_t)mac_addr[4] << 8)  | (uint32_t)mac_addr[5];
     ETH->MACA0HR = mac_hi | ETH_MACA0HR_MO;
     ETH->MACA0LR = mac_lo;
 
     ETH->MACCR = ETH_MACCR_IPCO | ETH_MACCR_IFG_96 | ETH_MACCR_DM |
#if ETH_FORCE_10M
                  /* FES=0 -> 10M (matches PHY forced to advertise 10M only) */
#else
                  ETH_MACCR_FES_100 |
#endif
                  ETH_MACCR_CSD | ETH_MACCR_TE | ETH_MACCR_RE
#if MAC_LOOPBACK_TEST
                  | ETH_MACCR_LM   /* internal loopback: TX -> own RX, PHY bypassed */
#endif
                  ;
#if MAC_LOOPBACK_TEST
     printf("[MAC] *** LOOPBACK TEST MODE - TX loops to RX, PHY/RMII bypassed ***\r\n");
#endif
     ETH->MACFFR = ETH_MACFFR_HPF | ETH_MACFFR_RA;
     ETH->MACFCR = 0x00001020;
 }
 
 /* ===== DMA Initialization ===== */
 void ETH_DMA_Init(ETH_DMADESCTypeDef *rx_desc, uint8_t *rx_buf, uint32_t rx_count,
                   ETH_DMADESCTypeDef *tx_desc, uint8_t *tx_buf, uint32_t tx_count,
                   uint32_t rx_buf_size, uint32_t tx_buf_size) {
    /* Reset DMA to clear error state from HAL_ETH_Init */
    ETH->DMABMR |= ETH_DMABMR_SR;
    while (ETH->DMABMR & ETH_DMABMR_SR);
    /* Clear DMASR sticky status bits (write 1 to clear) */
    ETH->DMASR = 0x00018404;  /* clear TBU|FBE|AIS|NIS */
    printf("[DMA] DMASR_AFTER_CLEAR=0x%08X (expect 0x00000000)\r\n", (uint32_t)ETH->DMASR);
    printf("[DMA] RX_DESC=0x%08X RX_BUF=0x%08X TX_DESC=0x%08X TX_BUF=0x%08X\r\n",
           (uint32_t)rx_desc, (uint32_t)rx_buf,
           (uint32_t)tx_desc, (uint32_t)tx_buf);

    ETH_DMADESCTypeDef *d;
 
     /* RX descriptors chain */
     for (uint32_t i = 0; i < rx_count; i++) {
         d = &rx_desc[i];
         d->Status = ETH_DMARxDesc_OWN;
         d->ControlBufferSize = ETH_DMARxDesc_RCH | (rx_buf_size & ETH_DMARxDesc_BSIZE);
         d->Buffer1Addr = (uint32_t)(rx_buf + i * rx_buf_size);
         if (i < rx_count - 1)
             d->Buffer2NextDescAddr = (uint32_t)(&rx_desc[i + 1]);
         else
             d->Buffer2NextDescAddr = (uint32_t)(&rx_desc[0]);
     }
 
     /* TX descriptors chain */
     for (uint32_t i = 0; i < tx_count; i++) {
         d = &tx_desc[i];
        d->Status = ETH_DMATxDesc_TCH;
         d->ControlBufferSize = 0;
         d->Buffer1Addr = (uint32_t)(tx_buf + i * tx_buf_size);
         if (i < tx_count - 1)
             d->Buffer2NextDescAddr = (uint32_t)(&tx_desc[i + 1]);
         else
             d->Buffer2NextDescAddr = (uint32_t)(&tx_desc[0]);
     }
 
     ETH->DMABMR = ETH_DMABMR_AAB | ETH_DMABMR_MB | ETH_DMABMR_FB | ETH_DMABMR_RDP_32 | ETH_DMABMR_PBL_32;
     ETH->DMARDLAR = (uint32_t)rx_desc;
     ETH->DMATDLAR = (uint32_t)tx_desc;
     ETH->DMAOMR = ETH_DMAOMR_OSF | ETH_DMAOMR_DTCEFD | ETH_DMAOMR_TSF | ETH_DMAOMR_FTF |
                   ETH_DMAOMR_TTC_64 | ETH_DMAOMR_ST | ETH_DMAOMR_SR;
 }
 
 /* ===== RX Packet Handling ===== */
 uint32_t ETH_GetRxPktSize(ETH_DMADESCTypeDef *rx_desc) {
     if (rx_desc->Status & ETH_DMARxDesc_OWN) return 0;
     if (!(rx_desc->Status & ETH_DMARxDesc_LS)) return 0;
     if (rx_desc->Status & ETH_DMARxDesc_ES) return 0;
     return (rx_desc->Status & ETH_DMARxDesc_FrameLength) >> 16;
 }
 
 void ETH_ReleaseRxDesc(ETH_DMADESCTypeDef *rx_desc, uint8_t *rx_buf,
                        uint32_t rx_count, uint32_t rx_buf_size) {
     rx_desc->ControlBufferSize = ETH_DMARxDesc_RCH | (rx_buf_size & ETH_DMARxDesc_BSIZE);
     rx_desc->Status = ETH_DMARxDesc_OWN;
 }
 
 void ETH_DMATransmissionRequest(void) {
     ETH->DMASR = ETH_DMASR_TPS;  /* clear TP bit */
     ETH->DMATPDR = 0;
 }
 
 void ETH_DMAClearITPendingBit(uint32_t bit) {
     ETH->DMASR = bit;
 }
