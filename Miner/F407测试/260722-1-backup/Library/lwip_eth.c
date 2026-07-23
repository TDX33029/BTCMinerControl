 #include "lwip_eth.h"
 
 /* ===== SMI (MDC/MDIO) Access ===== */
 uint16_t ETH_ReadPHYRegister(uint16_t phy, uint16_t reg) {
     uint32_t tmpreg = 0;
     tmpreg = ((uint32_t)phy << 11) | ((uint32_t)reg << 6) | ETH_MACMIIAR_MB;
     ETH->MACMIIAR = tmpreg;
     while (ETH->MACMIIAR & ETH_MACMIIAR_MB);
     return (uint16_t)(ETH->MACMIIDR & ETH_MACMIIDR_MD);
 }
 
 void ETH_WritePHYRegister(uint16_t phy, uint16_t reg, uint16_t val) {
     ETH->MACMIIDR = (uint32_t)val;
     uint32_t tmpreg = ((uint32_t)phy << 11) | ((uint32_t)reg << 6) | ETH_MACMIIAR_MW | ETH_MACMIIAR_MB;
     ETH->MACMIIAR = tmpreg;
     while (ETH->MACMIIAR & ETH_MACMIIAR_MB);
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
                  ETH_MACCR_FES_100 | ETH_MACCR_CSD | ETH_MACCR_TE | ETH_MACCR_RE;
     ETH->MACFFR = ETH_MACFFR_HPF | ETH_MACFFR_RA;
     ETH->MACFCR = 0x00001020;
 }
 
 /* ===== DMA Initialization ===== */
 void ETH_DMA_Init(ETH_DMADESCTypeDef *rx_desc, uint8_t *rx_buf, uint32_t rx_count,
                   ETH_DMADESCTypeDef *tx_desc, uint8_t *tx_buf, uint32_t tx_count,
                   uint32_t rx_buf_size, uint32_t tx_buf_size) {
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
