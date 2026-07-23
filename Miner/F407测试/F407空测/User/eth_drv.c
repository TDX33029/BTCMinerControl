 /**
  * F407 Ethernet Driver — RMII + LAN8720A PHY + LwIP raw API
  *
  * Board: 野火霸天虎V2 (STM32F407ZGT6)
  * PHY:   LAN8720A (RMII interface)
  */
 
 #include "eth_drv.h"
 #include "Delay.h"
 #include <string.h>
 
 #include "../Library/lwip_eth.h"
 
 /* LwIP headers */
 #include "lwip/init.h"
 #include "lwip/netif.h"
 #include "lwip/tcp.h"
 #include "lwip/timeouts.h"
 #include "netif/etharp.h"
 
 /* ===== PHY ===== */
#if defined(STM32F40_41xxx)
#define PHY_ADDR    0x00   /* LAN8720A */
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
#define PHY_ADDR    0x01   /* DP83848 */
#else
#define PHY_ADDR    0x00
#endif
 
 /* ===== ETH DMA ===== */
 #define ETH_RX_DESC_COUNT   4
 #define ETH_TX_DESC_COUNT   2
 #define ETH_BUF_SIZE        1520
 #define TCP_RX_RING_SIZE    4096
 
 /* Aligned ETH buffers */
 __attribute__((aligned(4)))
 ETH_DMADESCTypeDef  ETH_Rx_Desc[ETH_RX_DESC_COUNT];
 __attribute__((aligned(4)))
 ETH_DMADESCTypeDef  ETH_Tx_Desc[ETH_TX_DESC_COUNT];
 __attribute__((aligned(4)))
 uint8_t             ETH_Rx_Buff[ETH_RX_DESC_COUNT][ETH_BUF_SIZE];
 __attribute__((aligned(4)))
 uint8_t             ETH_Tx_Buff[ETH_TX_DESC_COUNT][ETH_BUF_SIZE];
 
 /* ===== LwIP Globals ===== */
 static struct netif     eth_netif;
 static ip_addr_t        dest_ip_addr;
 static uint16_t         dest_port;
 static struct tcp_pcb  *tcp_pcba = NULL;
 static volatile int     tcp_state = 0;  /* 0=closed,1=connecting,2=connected */
 
 /* TCP RX ring buffer */
 static uint8_t           rx_ring[TCP_RX_RING_SIZE];
 static volatile uint16_t rx_head = 0;
 static volatile uint16_t rx_tail = 0;
 
 /* Config storage */
 static uint8_t eth_mac[6];
 static uint8_t eth_ip[4];
 static uint8_t eth_gw[4];
 static uint8_t eth_mask[4];
 static uint16_t eth_lport;
 
 /* ===== LwIP TCP Callbacks ===== */
 static err_t tcp_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t e);
 static void tcp_err_cb(void *arg, err_t e);
 
 static err_t tcp_connected_cb(void *arg, struct tcp_pcb *pcb, err_t e) {
     if (e != ERR_OK) { tcp_state = -1; return ERR_ABRT; }
     tcp_state = 2;
     tcp_recv(pcb, tcp_recv_cb);
     tcp_err(pcb, tcp_err_cb);
     return ERR_OK;
 }
 
 static err_t tcp_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t e) {
     if (p == NULL) { tcp_state = 0; return ERR_OK; }
     struct pbuf *q = p;
     while (q) {
         for (uint16_t i = 0; i < q->len; i++) {
             uint16_t n = (rx_head + 1) % TCP_RX_RING_SIZE;
             if (n != rx_tail) { rx_ring[rx_head] = ((uint8_t *)q->payload)[i]; rx_head = n; }
         }
         q = q->next;
     }
     tcp_recved(pcb, p->tot_len);
     pbuf_free(p);
     return ERR_OK;
 }
 
 static void tcp_err_cb(void *arg, err_t e) {
     tcp_state = 0;
     tcp_pcba = NULL;
 }
 
 /* ===== RMII GPIO Init (F407) ===== */
 static void rmii_gpio_init(void) {
     GPIO_InitTypeDef g;
 
    /* Enable clocks */
#if defined(STM32F40_41xxx)
    /* F4: ETH MAC clocks on AHB1, GPIO clocks on AHB1 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_ETH_MAC |
                           RCC_AHB1Periph_ETH_MAC_Tx |
                           RCC_AHB1Periph_ETH_MAC_Rx, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA |
                           RCC_AHB1Periph_GPIOB |
                           RCC_AHB1Periph_GPIOC |
                           RCC_AHB1Periph_GPIOD, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    /* F1: ETH + GPIO clocks on APB2, use AFIO for RMII select */
    RCC->APB2ENR |= 0x00004000;  /* ETH PLL clk gate (APB2ENR bit14) */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_ETH_MAC |
                          RCC_AHBPeriph_ETH_MAC_Tx |
                          RCC_AHBPeriph_ETH_MAC_Rx, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC |
                           RCC_APB2Periph_GPIOD |
                           RCC_APB2Periph_AFIO, ENABLE);
#endif
 
#if defined(STM32F40_41xxx)
    /* ===== PA1 = REF_CLK (AF input) ===== */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource1, GPIO_AF_ETH);
    g.GPIO_Pin   = GPIO_Pin_1;
    g.GPIO_Mode  = GPIO_Mode_AF;
    g.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    g.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOA, &g);
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    /* ===== PA1 = REF_CLK (floating input) ===== */
    g.GPIO_Pin   = GPIO_Pin_1;
    g.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &g);
#endif
#if defined(STM32F40_41xxx)
    /* ===== PA2 = MDIO (AF output PP) ===== */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_ETH);
    g.GPIO_Pin   = GPIO_Pin_2;
    g.GPIO_Mode  = GPIO_Mode_AF;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    g.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOA, &g);
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    /* ===== PA2 = MDIO (AF push-pull) ===== */
    g.GPIO_Pin   = GPIO_Pin_2;
    g.GPIO_Mode  = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &g);
#endif
#if defined(STM32F40_41xxx)
    /* ===== PA7 = CRS_DV (AF input) ===== */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_ETH);
    g.GPIO_Pin   = GPIO_Pin_7;
    g.GPIO_Mode  = GPIO_Mode_AF;
    g.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    g.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOA, &g);
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    /* ===== PA7 = CRS_DV (floating input) ===== */
    g.GPIO_Pin   = GPIO_Pin_7;
    g.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &g);
#endif
    /* PB11/PB12 = TX_EN - pin differs between F4(PB11) and F1(PB12) */
#if defined(STM32F40_41xxx)
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_ETH);
    g.GPIO_Pin   = GPIO_Pin_11;
    g.GPIO_Mode  = GPIO_Mode_AF;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    g.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOB, &g);
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    /* F1: ETH_RMII_TX_EN on PB12 (user may remap) */
    g.GPIO_Pin   = GPIO_Pin_12;
    g.GPIO_Mode  = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &g);
#endif
    /* PB12/PB13 = TXD0 - same pin on F4 and F1 */
#if defined(STM32F40_41xxx)
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource12, GPIO_AF_ETH);
    g.GPIO_Pin   = GPIO_Pin_12;
    g.GPIO_Mode  = GPIO_Mode_AF;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    g.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOB, &g);
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    /* F1: ETH_RMII_TXD0 on PB13 */
    g.GPIO_Pin   = GPIO_Pin_13;
    g.GPIO_Mode  = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &g);
#endif
    /* PB13/PB15 = TXD1 - differs between F4(PB13) and F1(PB15) */
#if defined(STM32F40_41xxx)
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource13, GPIO_AF_ETH);
    g.GPIO_Pin   = GPIO_Pin_13;
    g.GPIO_Mode  = GPIO_Mode_AF;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    g.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOB, &g);
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    /* F1: ETH_RMII_TXD1 on PB15 */
    g.GPIO_Pin   = GPIO_Pin_15;
    g.GPIO_Mode  = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &g);
#endif
#if defined(STM32F40_41xxx)
    /* ===== PC1 = MDC (AF output PP) ===== */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource1, GPIO_AF_ETH);
    g.GPIO_Pin   = GPIO_Pin_1;
    g.GPIO_Mode  = GPIO_Mode_AF;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    g.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOC, &g);
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    /* ===== PC1 = MDC (AF push-pull) ===== */
    g.GPIO_Pin   = GPIO_Pin_1;
    g.GPIO_Mode  = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &g);
#endif
#if defined(STM32F40_41xxx)
    /* ===== PC4 = RXD0 (AF input) ===== */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource4, GPIO_AF_ETH);
    g.GPIO_Pin   = GPIO_Pin_4;
    g.GPIO_Mode  = GPIO_Mode_AF;
    g.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    g.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOC, &g);
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    /* ===== PC4 = RXD0 (floating input) ===== */
    g.GPIO_Pin   = GPIO_Pin_4;
    g.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &g);
#endif
#if defined(STM32F40_41xxx)
    /* ===== PC5 = RXD1 (AF input) ===== */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource5, GPIO_AF_ETH);
    g.GPIO_Pin   = GPIO_Pin_5;
    g.GPIO_Mode  = GPIO_Mode_AF;
    g.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    g.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOC, &g);
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    /* ===== PC5 = RXD1 (floating input) ===== */
    g.GPIO_Pin   = GPIO_Pin_5;
    g.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &g);
#endif
 
     /* ===== Select RMII mode (SYSCFG PMC bit7) ===== */
#if defined(STM32F40_41xxx)
    /* F4: SYSCFG PMC bit7 = MII/RMII select */
    SYSCFG->PMC |= (uint32_t)0x80;
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    /* F1: AFIO MAPR bit23 = MII/RMII select */
    AFIO->MAPR |= AFIO_MAPR_MII_RMII_SEL;
#endif
 }
 
 /* ===== PHY Init (LAN8720A) ===== */
 static int phy_init(void) {
    /* LAN8720A reset via NRST (no dedicated GPIO, use software reset only) */
    Delay_ms(50);
 
     uint16_t id1 = ETH_ReadPHYRegister(PHY_ADDR, 0x02);
     uint16_t id2 = ETH_ReadPHYRegister(PHY_ADDR, 0x03);
     if (id1 == 0xFFFF || id1 == 0x0000) return 0;
     if (id2 == 0xFFFF || id2 == 0x0000) return 0;
 
     /* Software reset */
     ETH_WritePHYRegister(PHY_ADDR, 0x00, 0x8000);
     uint32_t t = 0xFFFFFF;
     while (ETH_ReadPHYRegister(PHY_ADDR, 0x00) & 0x8000) { if (!--t) return 0; }
     Delay_ms(5);
 
     /* Auto-negotiate */
     ETH_WritePHYRegister(PHY_ADDR, 0x00, 0x1200);
     t = 0xFFFFFF;
     while (!(ETH_ReadPHYRegister(PHY_ADDR, 0x01) & 0x0020)) { if (!--t) return 0; }
 
     return 1;
 }
 
 /* ===== LwIP netif ===== */
 static err_t eth_output(struct netif *nf, struct pbuf *p) {
     if (ETH_Tx_Desc[0].Status & ETH_DMATxDesc_OWN) return ERR_BUF;
 
     uint8_t *tx = (uint8_t *)ETH_Tx_Desc[0].Buffer1Addr;
     uint32_t tot = 0;
     for (struct pbuf *q = p; q; q = q->next) {
         memcpy(tx + tot, q->payload, q->len);
         tot += q->len;
     }
     ETH_Tx_Desc[0].ControlBufferSize = tot;
     ETH_Tx_Desc[0].Status |= ETH_DMATxDesc_LS | ETH_DMATxDesc_FS |
                              ETH_DMATxDesc_IC | ETH_DMATxDesc_TCH;
     ETH_DMATransmissionRequest();
 
     uint32_t to = 0xFFFFF;
     while ((ETH_Tx_Desc[0].Status & ETH_DMATxDesc_OWN) && --to);
     return (to > 0) ? ERR_OK : ERR_TIMEOUT;
 }
 
 static err_t netif_init_cb(struct netif *nf) {
     nf->name[0] = 'e'; nf->name[1] = 't';
     nf->output = etharp_output;
     nf->linkoutput = eth_output;
     nf->mtu = 1500;
     nf->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
     nf->hwaddr_len = 6;
     memcpy(nf->hwaddr, eth_mac, 6);
     return ERR_OK;
 }
 
 static void eth_recv_poll(void) {
     uint32_t len = ETH_GetRxPktSize(ETH_Rx_Desc);
     if (len == 0 || len > ETH_BUF_SIZE) return;
     struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_RAM);
     if (p) {
         pbuf_take(p, (uint8_t *)ETH_Rx_Desc->Buffer1Addr, len);
         eth_netif.input(p, &eth_netif);
     }
     ETH_ReleaseRxDesc(ETH_Rx_Desc, ETH_Rx_Buff[0], ETH_RX_DESC_COUNT, ETH_BUF_SIZE);
 }
 
 /* ===== Public API ===== */
 void eth_gpio_init(void)  { rmii_gpio_init(); }
 void eth_reset(void)     { }
 
 int eth_init(const eth_config_t *cfg) {
     memcpy(eth_mac,   cfg->mac,    6);
     memcpy(eth_ip,    cfg->ip,     4);
     memcpy(eth_gw,    cfg->gateway, 4);
     memcpy(eth_mask,  cfg->subnet, 4);
     eth_lport = cfg->local_port;
 
     if (!phy_init()) return 0;
 
     ETH_SoftwareReset();
     while (ETH_GetSoftwareResetStatus());
     ETH_MAC_Init(eth_mac);
     ETH_DMA_Init(ETH_Rx_Desc, ETH_Rx_Buff[0], ETH_RX_DESC_COUNT,
                  ETH_Tx_Desc, ETH_Tx_Buff[0], ETH_TX_DESC_COUNT,
                  ETH_BUF_SIZE, ETH_BUF_SIZE);
 
     lwip_init();
 
     ip_addr_t ip, gw, mask;
     IP4_ADDR(&gw,   cfg->gateway[0], cfg->gateway[1], cfg->gateway[2], cfg->gateway[3]);
     IP4_ADDR(&ip,   cfg->ip[0],      cfg->ip[1],      cfg->ip[2],      cfg->ip[3]);
     IP4_ADDR(&mask, cfg->subnet[0],  cfg->subnet[1],  cfg->subnet[2],  cfg->subnet[3]);
 
     netif_add(&eth_netif, &ip, &mask, &gw, NULL, netif_init_cb, ethernet_input);
     netif_set_default(&eth_netif);
     netif_set_up(&eth_netif);
     return 1;
 }
 
 int eth_connect(uint8_t dest[4], uint16_t port) {
     if (tcp_pcba) { tcp_abort(tcp_pcba); tcp_pcba = NULL; }
     tcp_state = 1;
     rx_head = rx_tail = 0;
     IP4_ADDR(&dest_ip_addr, dest[0], dest[1], dest[2], dest[3]);
     dest_port = port;
 
     struct tcp_pcb *pcb = tcp_new();
     if (!pcb) return 0;
     tcp_bind(pcb, IP_ADDR_ANY, eth_lport);
     if (tcp_connect(pcb, &dest_ip_addr, port, tcp_connected_cb) != ERR_OK) {
         tcp_abort(pcb); return 0;
     }
     tcp_pcba = pcb;
 
     extern volatile uint32_t g_ms;
     uint32_t start = g_ms;
     while (tcp_state == 1 && (g_ms - start) < 3000) eth_poll();
     return (tcp_state == 2) ? 1 : 0;
 }
 
 int eth_send(const uint8_t *data, uint16_t len) {
     if (!tcp_pcba || tcp_state != 2) return -1;
     if (tcp_write(tcp_pcba, data, len, TCP_WRITE_FLAG_COPY) != ERR_OK) return -2;
     tcp_output(tcp_pcba);
     eth_poll();
     return len;
 }
 
 int eth_recv(uint8_t *buf, uint16_t buf_len) {
     eth_poll();
     if (tcp_state != 2) return -1;
     uint16_t n = 0;
     while (n < buf_len && rx_head != rx_tail) {
         buf[n++] = rx_ring[rx_tail];
         rx_tail = (rx_tail + 1) % TCP_RX_RING_SIZE;
     }
     return (int)n;
 }
 
 int eth_is_connected(void)  { return (tcp_state == 2) ? 1 : 0; }
 int eth_is_disconnected(void) { eth_poll(); return (tcp_state <= 0); }
 uint8_t eth_socket_status(void) { return (tcp_state == 2) ? 0x17 : (tcp_state == 1) ? 0x13 : 0x00; }
 void eth_close(void) { if (tcp_pcba) { tcp_abort(tcp_pcba); tcp_pcba = NULL; } tcp_state = 0; }
 
 void eth_poll(void) {
     eth_recv_poll();
     sys_check_timeouts();
 }
 
 
 
uint32_t sys_now(void) {
    extern volatile uint32_t g_ms;
    return g_ms;
}

/* ===== LwIP Critical Section (NO_SYS bare-metal) ===== */
sys_prot_t sys_arch_protect(void) {
    __disable_irq();
    return 0;
}

void sys_arch_unprotect(sys_prot_t pval) {
    __enable_irq();
    (void)pval;
}
