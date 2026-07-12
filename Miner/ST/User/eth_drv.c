/**
 * 浠ュお缃戦┍鍔?鈥?F107 MAC + DP83848 PHY + LwIP raw API
 *
 * 瀵瑰鎺ュ彛瀹屽叏鍏煎 w5500.h (main.c 鏃犻渶淇敼)銆?
 */

/* LwIP 鈥?瑁告満 raw API銆傞渶瑕?LwIP 婧愮爜鍦?User/lwip/ 鐩綍涓嬨€?
 * 濡傛灉娌℃湁 LwIP锛屽彲浠ラ缂栬瘧 lib 鎴栫敤 ST 瀹樻柟 LwIP 鍖呫€?
 * 褰撳墠鐗堟湰: 濡傛灉缂?LwIP锛岀紪璇戞椂瀹氫箟 ETH_NO_LWIP 璺宠繃 TCP 鍔熻兘銆?*/
#ifdef ETH_NO_LWIP
/* Stub 瀹炵幇 (鏃?LwIP) */
#include "eth_drv.h"
#include "Delay.h"
#include <string.h>
void eth_gpio_init(void) {}
void eth_reset(void) {}
int  eth_init(const eth_config_t *c) { (void)c; return 0; }
int  eth_connect(uint8_t d[4], uint16_t p) { (void)d; (void)p; return 0; }
int  eth_send(const uint8_t *d, uint16_t l) { (void)d; (void)l; return -1; }
int  eth_recv(uint8_t *b, uint16_t l) { (void)b; (void)l; return -1; }
int  eth_is_connected(void) { return 0; }
int  eth_is_disconnected(void) { return 1; }
uint8_t eth_socket_status(void) { return 0; }
void eth_close(void) {}
void eth_poll(void) {}
#else

#include "eth_drv.h"
#include "Delay.h"
#include <string.h>

/* 瀵勫瓨鍣ㄧ骇 ETH 椹卞姩 (鏇夸唬 stm32f10x_eth.h) */
#include "../Library/lwip_eth.h"

/* LwIP */
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"

/* ===== 甯搁噺 ===== */
#define DP83848_PHY_ADDR    0x01
#define ETH_RX_DESC_COUNT   4
#define ETH_TX_DESC_COUNT   2
#define ETH_BUF_SIZE        1520
#define TCP_RX_RING_SIZE    4096

/* ===== ETH DMA 鎻忚堪绗﹀拰缂撳啿鍖?(4 瀛楄妭瀵归綈) ===== */
__ALIGN_BEGIN ETH_DMADESCTypeDef  ETH_Rx_Desc[ETH_RX_DESC_COUNT] __ALIGN_END;
__ALIGN_BEGIN ETH_DMADESCTypeDef  ETH_Tx_Desc[ETH_TX_DESC_COUNT] __ALIGN_END;
__ALIGN_BEGIN uint8_t             ETH_Rx_Buff[ETH_RX_DESC_COUNT][ETH_BUF_SIZE] __ALIGN_END;
__ALIGN_BEGIN uint8_t             ETH_Tx_Buff[ETH_TX_DESC_COUNT][ETH_BUF_SIZE] __ALIGN_END;

/* ===== LwIP 鍏ㄥ眬瀵硅薄 ===== */
static struct netif     eth_netif;
static ip_addr_t        dest_ip_addr;
static uint16_t         dest_port;
static struct tcp_pcb  *tcp_pcba = NULL;
static volatile int     tcp_state = 0;  /* 0=closed,1=connecting,2=connected */

/* TCP 鎺ユ敹鐜舰缂撳啿鍖?*/
static uint8_t           rx_ring[TCP_RX_RING_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

/* 閰嶇疆 */
static uint8_t eth_mac[6];
static uint8_t eth_ip[4];
static uint8_t eth_gw[4];
static uint8_t eth_mask[4];
static uint16_t eth_lport;

/* ===== LwIP TCP 鍥炶皟 ===== */
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

/* ===== RMII GPIO 鍒濆鍖?(鏇夸唬 SPI) ===== */
static void rmii_gpio_init(void) {
    GPIO_InitTypeDef g;
    /* [fix] RCC_APB2Periph_ETH not defined in library V3.5; use direct reg access */
    RCC->APB2ENR |= 0x00004000;  /* ETH PLL clk gate (APB2ENR bit14) */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_ETH_MAC | RCC_AHBPeriph_ETH_MAC_Tx | RCC_AHBPeriph_ETH_MAC_Rx, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC |
                           RCC_APB2Periph_GPIOD | RCC_APB2Periph_AFIO, ENABLE);

    /* PA1=REF_CLK (RMII 50MHz), PA7=CRS_DV (RMII) */
    g.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    g.GPIO_Pin = GPIO_Pin_1;              GPIO_Init(GPIOA, &g);
    g.GPIO_Pin = GPIO_Pin_7;              GPIO_Init(GPIOA, &g);
    /* PA2=MDIO (AF PP) */
    g.GPIO_Pin = GPIO_Pin_2; g.GPIO_Mode = GPIO_Mode_AF_PP; g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &g);

    /* PB11=TX_EN, PB12=TX_D0, PB13=TX_D1 */
    g.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13;
    g.GPIO_Mode = GPIO_Mode_AF_PP; g.GPIO_Speed = GPIO_Speed_50MHz; GPIO_Init(GPIOB, &g);

    /* PC4=RX_D0, PC5=RX_D1, PC1=MDC */
    g.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5; g.GPIO_Mode = GPIO_Mode_IN_FLOATING; GPIO_Init(GPIOC, &g);
    g.GPIO_Pin = GPIO_Pin_1; g.GPIO_Mode = GPIO_Mode_AF_PP; g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &g);

    /* PD2=ETH_RST (杈撳嚭, 鍒濆浣? */
    g.GPIO_Pin = GPIO_Pin_2; g.GPIO_Mode = GPIO_Mode_Out_PP; g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOD, &g);
    GPIO_ResetBits(GPIOD, GPIO_Pin_2);
    /* [fix] Select RMII mode (was MII default - wrong pinout!) */
    AFIO->MAPR |= AFIO_MAPR_MII_RMII_SEL;
}

/* ===== PHY 鍒濆鍖?===== */
static int phy_init(void) {
    GPIO_SetBits(GPIOD, GPIO_Pin_2);   Delay_ms(20);
    GPIO_ResetBits(GPIOD, GPIO_Pin_2); Delay_ms(20);
    GPIO_SetBits(GPIOD, GPIO_Pin_2);   Delay_ms(20);

    uint16_t id1 = ETH_ReadPHYRegister(DP83848_PHY_ADDR, 0x02);
    if (id1 == 0xFFFF || id1 == 0x0000) return 0;

    /* 杞欢澶嶄綅 */
    ETH_WritePHYRegister(DP83848_PHY_ADDR, 0x00, 0x8000);
    Delay_ms(5);
    uint32_t t = 0xFFFFF;
    while (ETH_ReadPHYRegister(DP83848_PHY_ADDR, 0x00) & 0x8000) { if (!--t) return 0; }

    /* 鑷姩鍗忓晢 */
    ETH_WritePHYRegister(DP83848_PHY_ADDR, 0x00, 0x1000);
    t = 0xFFFFF;
    while (!(ETH_ReadPHYRegister(DP83848_PHY_ADDR, 0x01) & 0x0020)) { if (!--t) return 0; }

    return 1;
}

/* ===== LwIP 鍙戦€?(ethernetif) ===== */
static err_t eth_output(struct netif *nf, struct pbuf *p) {
    if (ETH_Tx_Desc[0].Status & ETH_DMATxDesc_OWN) return ERR_BUF;

    uint8_t *tx = (uint8_t *)ETH_Tx_Desc[0].Buffer1Addr;
    uint32_t tot = 0;
    for (struct pbuf *q = p; q; q = q->next) {
        memcpy(tx + tot, q->payload, q->len); tot += q->len;
    }
    ETH_Tx_Desc[0].ControlBufferSize = tot;
    ETH_Tx_Desc[0].Status |= ETH_DMATxDesc_LS | ETH_DMATxDesc_FS | ETH_DMATxDesc_IC | ETH_DMATxDesc_TCH;
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

/* ===== 鍏紑鎺ュ彛 (涓?w5500.h 涓€鑷? ===== */
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

    /* 绛夊緟杩炴帴 (鏈€澶?3 绉? */
    uint32_t start = *(volatile uint32_t *)0xE000E018; /* SysTick VAL 鈥?鐢?g_ms 鏇村ソ */
    extern volatile uint32_t g_ms;
    start = g_ms;
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

/* ===== 椹卞姩杞 (鏇夸唬 SPI 杞) ===== */
void eth_poll(void) {
    eth_recv_poll();
    sys_check_timeouts();
}

/* ethernet_input: LwIP 鏈熸湜鐨勮８鏈烘帴鏀跺叆鍙?(鐢?eth_recv_poll 闂存帴璋冪敤, 杩欓噷鐣欑┖) */
void ethernet_input(struct pbuf *p, struct netif *nf) {
    nf->input(p, nf);
}

uint32_t sys_now(void) {
    extern volatile uint32_t g_ms;
    return g_ms;
}

#endif /* ETH_NO_LWIP */
