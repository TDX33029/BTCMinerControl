#include "eth_netif.h"
#include "eth_drv.h"
#include "lwip_eth.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/tcp.h"
#include "lwip/ip4_addr.h"
#include "lwip/etharp.h"
#include "lwip/timeouts.h"
#include "lwip/dhcp.h"
#include "netif/ethernet.h"
#include <string.h>
#include <stdio.h>

/* ===== Static IP config (used when ETH_USE_STATIC_IP is 1) =====
   Set to 1 to skip DHCP and use a fixed IPv4 config. Pick an address OUTSIDE
   the router's DHCP pool to avoid conflicts. This is a diagnostic to confirm
   whether the DHCP long-frame (~345B) RX corruption is the only blocker: if
   the board connects stably with static IP (only short ARP/TCP frames), the
   RMII is fine for short frames and only DHCP's long frames expose it. */
#define ETH_USE_STATIC_IP    1
#define ETH_STATIC_IP0       10
#define ETH_STATIC_IP1       8
#define ETH_STATIC_IP2       1
#define ETH_STATIC_IP3       50    /* pick one outside your DHCP pool */
#define ETH_STATIC_GW0       10
#define ETH_STATIC_GW1       8
#define ETH_STATIC_GW2       1
#define ETH_STATIC_GW3       1     /* router */
#define ETH_STATIC_NM0       255
#define ETH_STATIC_NM1       255
#define ETH_STATIC_NM2       255
#define ETH_STATIC_NM3       0

/* ===== External ETH handle from CubeMX ===== */
extern ETH_HandleTypeDef heth;

/* ===== Local state ===== */
static int tcp_connected = 0;

/* ===== LwIP state ===== */
static struct netif    eth_netif;
static struct tcp_pcb *tcp_pcb = NULL;
static ip_addr_t       server_ip;
static uint16_t        server_port = 0;
static uint32_t        tcp_connect_start = 0;

/* ===== TCP receive ring buffer ===== */
#define TCP_RX_BUF_SIZE  4096
static uint8_t  tcp_rx_buf[TCP_RX_BUF_SIZE];
static uint16_t tcp_rx_head = 0;
static uint16_t tcp_rx_tail = 0;

/* ===== PHY Reset via PD2 ===== */
static void phy_reset(void) {
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_SET);
    HAL_Delay(300);
}

/* ===== ETH Init - uses lwip_eth.c direct register access ===== */
int eth_init(const eth_config_t *cfg) {
    /* Reset PHY */
    phy_reset();

#if ETH_FORCE_10M
    /* Force 10M: advertise only 10FD+10HD and restart auto-negotiation so the
       link comes up at 10M, matching the MAC (FES=0 in ETH_MAC_Init).
       Workaround for 100M RMII RX CRC/dribble-bit errors. No pin changes. */
    ETH_WritePHYRegister(DP83848_PHY_ADDRESS, 0x04, 0x0061);  /* ANAR: 10FD + 10HD only */
    ETH_WritePHYRegister(DP83848_PHY_ADDRESS, 0x00, 0x1200);  /* BCR: restart auto-neg */
    HAL_Delay(2000);
    printf("[PHY] Forced 10M (ETH_FORCE_10M=1)\r\n");
#endif

    ethernetif_dma_init();
    ETH_MAC_Init((uint8_t *)cfg->mac);
    /* Debug: check DMA and RMII state */
    printf("[DMA] DMASR=0x%08X  DMABMR=0x%08X  DMAOMR=0x%08X\r\n",
           (uint32_t)ETH->DMASR, (uint32_t)ETH->DMABMR, (uint32_t)ETH->DMAOMR);
    printf("[DMA] MACCR=0x%08X  MACFFR=0x%08X\r\n",
           (uint32_t)ETH->MACCR, (uint32_t)ETH->MACFFR);
    {
        uint32_t mapr = 0;
#ifdef AFIO
        mapr = AFIO->MAPR;
#elif defined(SYSCFG)
        mapr = SYSCFG->PMC;
#endif
        printf("[DMA] AFIO_MAPR=0x%08X  (bit24=ETH_RMII)\r\n", mapr);
    }

    /* Init LwIP stack */
    lwip_init();

    /* Add the netif. With DHCP, the IPv4 fields are 0.0.0.0 (filled at runtime
       by dhcp_start); with ETH_USE_STATIC_IP, they are the fixed config. */
    ip4_addr_t ip_address, gateway, netmask;
#if ETH_USE_STATIC_IP
    IP4_ADDR(&ip_address, ETH_STATIC_IP0, ETH_STATIC_IP1, ETH_STATIC_IP2, ETH_STATIC_IP3);
    IP4_ADDR(&gateway,    ETH_STATIC_GW0,  ETH_STATIC_GW1,  ETH_STATIC_GW2,  ETH_STATIC_GW3);
    IP4_ADDR(&netmask,    ETH_STATIC_NM0,  ETH_STATIC_NM1,  ETH_STATIC_NM2,  ETH_STATIC_NM3);
#else
    IP4_ADDR(&ip_address, 0, 0, 0, 0);
    IP4_ADDR(&gateway, 0, 0, 0, 0);
    IP4_ADDR(&netmask, 0, 0, 0, 0);
#endif

    if (netif_add(&eth_netif, &ip_address, &netmask, &gateway, NULL,
                  ethernetif_init, ethernet_input) == NULL) {
        printf("[LWIP] netif_add failed\r\n");
        return 0;
    }
    netif_set_default(&eth_netif);
    netif_set_up(&eth_netif);
    ethernetif_set_link(&eth_netif, 1);

    /* lwIP copies the source MAC for every outgoing frame from netif->hwaddr,
       so it must be set before dhcp_start() queues the DHCPDISCOVER. */
    memcpy(eth_netif.hwaddr, cfg->mac, 6);
    eth_netif.hwaddr_len = ETH_HWADDR_LEN;

    /* Unique DHCP hostname (option 12) from the UID-derived MAC suffix, so each
       board shows up with a distinct name in the router's DHCP lease table. The
       buffer is static because netif_set_hostname stores the pointer, not a copy. */
    static char dhcp_hostname[24];
    snprintf(dhcp_hostname, sizeof(dhcp_hostname), "BTCMiner-%02X%02X%02X",
             cfg->mac[3], cfg->mac[4], cfg->mac[5]);
    netif_set_hostname(&eth_netif, dhcp_hostname);

#if ETH_USE_STATIC_IP
    printf("[LWIP] Static IP %d.%d.%d.%d GW %d.%d.%d.%d NM %d.%d.%d.%d\r\n",
           ETH_STATIC_IP0, ETH_STATIC_IP1, ETH_STATIC_IP2, ETH_STATIC_IP3,
           ETH_STATIC_GW0, ETH_STATIC_GW1, ETH_STATIC_GW2, ETH_STATIC_GW3,
           ETH_STATIC_NM0, ETH_STATIC_NM1, ETH_STATIC_NM2, ETH_STATIC_NM3);
#else
    /* Start DHCP. The DHCP state machine is driven by sys_check_timeouts(),
       polled in eth_poll(). eth_has_ip() becomes true once the lease is bound,
       and the main loop delays the TCP connect until then. */
    dhcp_start(&eth_netif);
    printf("[LWIP] DHCP started (waiting for lease)...\r\n");
#endif
    return 1;
}

/* ===== ?? PHY ?????????????===== */
static void eth_deep_debug(void) {
    uint16_t pa = DP83848_PHY_ADDRESS;

    printf("[PHYDBG] === DP83848DSK 25MHz RMII ?? ===\r\n");

    /* ?????? 0x11-0x1F ?????? */
    uint16_t regs[15];
    for (int i = 0; i < 15; i++) {
        regs[i] = ETH_ReadPHYRegister(pa, 0x11 + i);
    }
    printf("[PHYDBG] Ext regs 0x11-0x1F:");
    for (int i = 0; i < 15; i++) {
        printf(" %04X", regs[i]);
    }
    printf("\r\n");

    /* DP83848 Strapping Status Register (?? 0x18 ? 0x19) */
    uint16_t strap18 = ETH_ReadPHYRegister(pa, 0x18);
    uint16_t strap19 = ETH_ReadPHYRegister(pa, 0x19);
    printf("[PHYDBG] STRAP(0x18)=0x%04X  STRAP(0x19)=0x%04X\r\n", strap18, strap19);

    /* Clock/CFG ??? 0x11 (PHYCR) ??? */
    printf("[PHYDBG] PHYCR(0x11)=0x%04X\r\n", regs[0]);

    /* ??? 3 ? reg 3 ???? */
    uint16_t r1 = ETH_ReadPHYRegister(pa, 3);
    uint16_t r2 = ETH_ReadPHYRegister(pa, 3);
    uint16_t r3 = ETH_ReadPHYRegister(pa, 3);
    printf("[PHYDBG] PHYIDR2 reads: 0x%04X 0x%04X 0x%04X  %s\r\n",
           r1, r2, r3,
           (r1 == r2 && r2 == r3) ? "stable" : "UNSTABLE!");

    if (r1 == r2 && r2 == r3) {
        if (r1 == 0xA0B0 || r1 == 0x5C90) {
            printf("[PHYDBG] PHY = DP83848 %s (confirmed)\r\n",
                   (r1 == 0xA0B0) ? "C/I" : "KSQ");
        } else {
            printf("[PHYDBG] Unrecognized PHY ID=0x%04X:0x%04X\r\n",
                   ETH_ReadPHYRegister(pa, 2), r1);
        }
    }

    /* ? MII ????? BMSR ??? link ?????? */
    /* ??????? BMSR??????????? */
    printf("[PHYDBG] Waiting 3s for autoneg to settle...\r\n");
    HAL_Delay(3000);  /* ?? 3 ??????? */
    uint16_t bmsr_final = ETH_ReadPHYRegister(pa, 1);
    uint16_t anlpar_final = ETH_ReadPHYRegister(pa, 5);
    uint16_t bcr = ETH_ReadPHYRegister(pa, 0);
    printf("[PHYDBG] === ?????? ===\r\n");
    printf("[PHYDBG] BCR=0x%04X  autoneg=%s  speed=%s  duplex=%s\r\n",
           bcr,
           (bcr & 0x1000) ? "EN" : "DIS",
           (bcr & 0x2000) ? "100M" : "10M",
           (bcr & 0x0100) ? "FULL" : "HALF");
    printf("[PHYDBG] BMSR=0x%04X  link=%s  autoneg_done=%s\r\n",
           bmsr_final,
           (bmsr_final & 0x0004) ? "UP!!!" : "DOWN",
           (bmsr_final & 0x0020) ? "DONE" : "PENDING");
    printf("[PHYDBG] ANLPAR=0x%04X  partner=%s\r\n",
           anlpar_final,
           anlpar_final ? "PRESENT" : "ABSENT");
    if (bmsr_final & 0x0004) {
        printf("[PHYDBG] *** ????????????? ***\r\n");
    }
}

/* ===== PHY Debug: ?? PHY ????? SMI ?? ===== */
void eth_print_phy_status(void) {
    eth_deep_debug();

    uint32_t macmiiar_val;
    int i, found = 0;

    /* ?????? MAC ???????? */
    printf("[PHY] ETH_BASE=0x%08X\r\n", (uint32_t)ETH);
    macmiiar_val = ETH->MACMIIAR;
    printf("[PHY] MACMIIAR=0x%08X  (expect MB=bit0=0, CR=bits4:2)\r\n", macmiiar_val);
    printf("[PHY]   MB=%d CR=%d.%d.%d MR=0x%02X PA=0x%02X MW=%d\r\n",
           (int)(macmiiar_val & 0x01),
           (int)((macmiiar_val >> 4) & 0x01),
           (int)((macmiiar_val >> 3) & 0x01),
           (int)((macmiiar_val >> 2) & 0x01),
           (int)((macmiiar_val >> 6) & 0x1F),
           (int)((macmiiar_val >> 11) & 0x1F),
           (int)((macmiiar_val >> 16) & 0x01));

    /* ?????? SMI ??, ?? MB ??? */
    {
        uint32_t before = ETH->MACMIIAR;
        uint16_t reader = ETH_ReadPHYRegister(0x01, 2);
        uint32_t after  = ETH->MACMIIAR;
        printf("[PHY] SMI test: before=0x%08X  after=0x%08X  result=0x%04X\r\n",
               before, after, reader);
    }

    /* ???????? PHY ?? (0x00 - 0x1F) */
    printf("[PHY] Scanning addresses 0x00-0x1F...\r\n");
    for (i = 0; i < 32; i++) {
        uint16_t id_hi = ETH_ReadPHYRegister(i, 2);
        uint16_t id_lo = ETH_ReadPHYRegister(i, 3);
        if (id_hi != 0x0000 && id_hi != 0xFFFF) {
            printf("[PHY]  Addr 0x%02X: ID=0x%04X:%04X\r\n", i, id_hi, id_lo);
            found = 1;
        }
    }
    if (!found) {
        printf("[PHY]  No PHY responded at any address!\r\n");
        /* ???? 0x01 ?????? */
        uint16_t test1 = ETH_ReadPHYRegister(0x01, 2);
        uint16_t test2 = ETH_ReadPHYRegister(0x01, 1);
        printf("[PHY]  Read 0x01 reg2=0x%04X reg1=0x%04X\r\n", test1, test2);
        return;
    }

    /* ?????????? PHY ?? */
    uint16_t phy_addr = DP83848_PHY_ADDRESS;
    uint16_t id_hi = ETH_ReadPHYRegister(phy_addr, 2);
    uint16_t id_lo = ETH_ReadPHYRegister(phy_addr, 3);
    uint16_t bmsr  = ETH_ReadPHYRegister(phy_addr, 1);

    printf("[PHY] DP83848 ID=0x%04X:%04X (expect 0x2000:A0B0)\r\n", id_hi, id_lo);
    printf("[PHY] BMSR=0x%04X  link=%s  autoneg=%s  autoneg_done=%s\r\n",
           bmsr,
           (bmsr & 0x0004) ? "UP! " : "DOWN",
           (bmsr & 0x1000) ? "ON" : "OFF",
           (bmsr & 0x0020) ? "DONE" : "PENDING");

    /* DP83848 PHYSTS ??? (0x10) */
    uint16_t physts = ETH_ReadPHYRegister(phy_addr, 0x10);
    printf("[PHY] PHYSTS=0x%04X  speed=%s  duplex=%s  page_rx=%s\r\n",
           physts,
           (physts & 0x0002) ? "10M" : "100M",
           (physts & 0x0004) ? "FULL" : "HALF",
           (physts & 0x0020) ? "OK" : "IDLE");

    /* ????? PHY ????? (BCR reg 0) ???? */
    uint16_t bcr = ETH_ReadPHYRegister(phy_addr, 0);
    printf("[PHY] BCR=0x%04X  reset=%s  autoneg=%s  speed=%s  duplex=%s  iso=%s  pd=%s\r\n",
           bcr,
           (bcr & 0x8000) ? "ON" : "done",
           (bcr & 0x1000) ? "EN" : "DIS",
           (bcr & 0x2000) ? "100M" : "10M",
           (bcr & 0x0100) ? "FULL" : "HALF",
           (bcr & 0x0400) ? "ISO" : "no",
           (bcr & 0x0800) ? "PD" : "up");

    /* ????ANAR ? ANLPAR ? ??????? */
    uint16_t anar   = ETH_ReadPHYRegister(phy_addr, 4);
    uint16_t anlpar = ETH_ReadPHYRegister(phy_addr, 5);
    printf("[PHY] ANAR=0x%04X  100FD=%d 100HD=%d 10FD=%d 10HD=%d  pause=%d\r\n",
           anar,
           (anar & 0x0100) ? 1 : 0,  /* 100BASE-TX full duplex */
           (anar & 0x0080) ? 1 : 0,  /* 100BASE-TX half duplex */
           (anar & 0x0040) ? 1 : 0,  /* 10BASE-T full duplex */
           (anar & 0x0020) ? 1 : 0,  /* 10BASE-T half duplex */
           (anar & 0x0400) ? 1 : 0); /* symmetric pause */
    printf("[PHY] ANLPAR=0x%04X  100FD=%d 100HD=%d 10FD=%d 10HD=%d  pause=%d\r\n",
           anlpar,
           (anlpar & 0x0100) ? 1 : 0,
           (anlpar & 0x0080) ? 1 : 0,
           (anlpar & 0x0040) ? 1 : 0,
           (anlpar & 0x0020) ? 1 : 0,
           (anlpar & 0x0400) ? 1 : 0);
    printf("[PHY] link_partner=%s\r\n",
           anlpar ? "PRESENT" : "ABSENT");
}

/* ===== ? PHY ???? ===== */
int eth_link_status(void) {
    uint16_t bmsr = ETH_ReadPHYRegister(DP83848_PHY_ADDRESS, 1);
    return (bmsr & 0x0004) ? 1 : 0;
}

/* ===== TCP callbacks (LwIP raw API) ===== */
static void tcp_client_err(void *arg, err_t err) {
    (void)arg;
    /* lwIP calls this when the connection is aborted (ERR_ABRT=-13, RST,
       retransmit timeout, keepalive failure). By the time the err callback
       fires, lwIP has ALREADY freed the PCB -- so we must NOT call
       tcp_abort()/tcp_close() on it (use-after-free -> hang). Just drop
       our pointer and let the main loop reconnect. */
    printf("[TCP] ERR callback: err=%d\r\n", (int)err);
    tcp_connected = 0;
    tcp_pcb = NULL;
}

static err_t tcp_client_poll(void *arg, struct tcp_pcb *pcb) {
    (void)arg; (void)pcb;
    return ERR_OK;
}

static err_t tcp_client_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg;
    if (err != ERR_OK || p == NULL) {
        /* Remote closed (or error). Must free the PCB -- without this the PCB
           sits in CLOSE_WAIT forever and after MEMP_NUM_TCP_PCB(=4) cycles
           tcp_new() returns NULL ("No PCB"). tcp_close() is safe in recv cb. */
        printf("[TCP] Connection closed\r\n");
        tcp_connected = 0;
        if (tcp_pcb) {
            if (tcp_close(pcb) != ERR_OK) {
                tcp_abort(pcb);
            }
            tcp_pcb = NULL;
        }
        return ERR_OK;
    }
    uint16_t total = p->tot_len;
    int overflow = 0;
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        uint8_t *src = (uint8_t *)q->payload;
        for (uint16_t i = 0; i < q->len; i++) {
            uint16_t next = (tcp_rx_head + 1) % TCP_RX_BUF_SIZE;
            if (next == tcp_rx_tail) {
                overflow = 1;
                break;
            }
            tcp_rx_buf[tcp_rx_head] = src[i];
            tcp_rx_head = next;
        }
        if (overflow) break;
    }
    if (overflow) {
        /* Never acknowledge and silently discard the middle of a framed TCP
           stream. Abort and reconnect so both endpoints restart at a frame
           boundary instead of interpreting payload bytes as a length field. */
        printf("[TCP] RX ring overflow; reconnecting\r\n");
        pbuf_free(p);
        tcp_connected = 0;
        tcp_pcb = NULL;
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    tcp_recved(pcb, total);
    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_client_connected(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err == ERR_OK) {
        tcp_connected = 1;
        tcp_nagle_disable(pcb);  /* send control replies without Nagle delay */
        printf("[TCP] Connected to %d.%d.%d.%d:%d\r\n",
               ip4_addr1(&server_ip), ip4_addr2(&server_ip),
               ip4_addr3(&server_ip), ip4_addr4(&server_ip), server_port);
        tcp_recv(pcb, tcp_client_recv);
        tcp_err(pcb, tcp_client_err);
        tcp_poll(pcb, tcp_client_poll, 2);
        /* Keepalive: send TCP probes when idle so an idle server / a dead link
           is detected/kept instead of silently timing out and closing.
           keep_intvl/keep_cnt only exist when LWIP_TCP_KEEPALIVE is enabled
           (lwipopts.h) -- guard them so this compiles either way. */
        pcb->so_options |= SOF_KEEPALIVE;
        pcb->keep_idle  = 10000U;  /* 10s idle before first probe */
#if LWIP_TCP_KEEPALIVE
        pcb->keep_intvl = 2000U;   /* 2s between probes */
        pcb->keep_cnt   = 3U;      /* 3 missed probes => drop */
#endif
    } else {
        printf("[TCP] Connect failed: %d\r\n", (int)err);
        tcp_connected = 0;
        tcp_pcb = NULL;
    }
    return err;
}

/* ===== Public API ===== */
int eth_connect(uint8_t dst[4], uint16_t port) {
    if (tcp_connected) return 1;
    if (tcp_pcb) return 0;
    IP4_ADDR(&server_ip, dst[0], dst[1], dst[2], dst[3]);
    server_port = port;
    tcp_pcb = tcp_new();
    if (!tcp_pcb) { printf("[TCP] No PCB\r\n"); return 0; }
    err_t err = tcp_bind(tcp_pcb, IP_ANY_TYPE, 0);
    if (err != ERR_OK) { tcp_abort(tcp_pcb); tcp_pcb = NULL; return 0; }
    printf("[TCP] Connect %d.%d.%d.%d:%d...\r\n",
           dst[0], dst[1], dst[2], dst[3], port);
    err = tcp_connect(tcp_pcb, &server_ip, port, tcp_client_connected);
    if (err != ERR_OK) { tcp_abort(tcp_pcb); tcp_pcb = NULL; return 0; }
    tcp_connect_start = HAL_GetTick();
    return 0;
}

int eth_send(const uint8_t *data, uint16_t len) {
    if (!tcp_connected || !tcp_pcb) return -1;
    err_t err = tcp_write(tcp_pcb, data, len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) return -1;
    tcp_output(tcp_pcb);
    return (int)len;
}

int eth_recv(uint8_t *buf, uint16_t buf_len) {
    uint16_t count = 0;
    while (count < buf_len && tcp_rx_tail != tcp_rx_head) {
        buf[count++] = tcp_rx_buf[tcp_rx_tail];
        tcp_rx_tail = (tcp_rx_tail + 1) % TCP_RX_BUF_SIZE;
    }
    return (int)count;
}

int eth_is_connected(void) { return tcp_connected; }

int eth_has_ip(void) {
    return !ip4_addr_isany(netif_ip4_addr(&eth_netif));
}

void eth_get_ip(uint8_t out[4]) {
    const ip4_addr_t *ip = netif_ip4_addr(&eth_netif);
    out[0] = ip4_addr1(ip);
    out[1] = ip4_addr2(ip);
    out[2] = ip4_addr3(ip);
    out[3] = ip4_addr4(ip);
}

void eth_poll(void) {
    ethernetif_input(&eth_netif);
    static uint32_t last_tmr = 0;
    uint32_t now = HAL_GetTick();
    if ((now - last_tmr) > 250) {
        last_tmr = now;
        sys_check_timeouts();
    }

    /* Announce the DHCP lease once it is bound. The DHCP timer fires inside
       sys_check_timeouts(); after OFFER/REQUEST/ACK the netif's IPv4 address,
       gateway and netmask are populated by LwIP. */
    static int ip_announced = 0;
    if (!ip_announced && !ip4_addr_isany(netif_ip4_addr(&eth_netif))) {
        ip_announced = 1;
        const ip4_addr_t *ip = netif_ip4_addr(&eth_netif);
        const ip4_addr_t *gw = netif_ip4_gw(&eth_netif);
        const ip4_addr_t *nm = netif_ip4_netmask(&eth_netif);
        printf("[LWIP] DHCP bound: IP %d.%d.%d.%d GW %d.%d.%d.%d NM %d.%d.%d.%d\r\n",
               ip4_addr1(ip), ip4_addr2(ip), ip4_addr3(ip), ip4_addr4(ip),
               ip4_addr1(gw), ip4_addr2(gw), ip4_addr3(gw), ip4_addr4(gw),
               ip4_addr1(nm), ip4_addr2(nm), ip4_addr3(nm), ip4_addr4(nm));
    }
    static uint32_t last_link = 0;
    if ((now - last_link) > 2000) {
        last_link = now;
        int phy_link = eth_link_status();
        if (!phy_link) {
            if (tcp_connected) { printf("[ETH] Link lost!\r\n"); tcp_connected = 0; }
            ethernetif_set_link(&eth_netif, 0);
        } else {
            ethernetif_set_link(&eth_netif, 1);
        }
    }
    if (!tcp_connected && tcp_pcb && (now - tcp_connect_start) > 5000) {
        printf("[TCP] Timeout, retry\r\n");
        tcp_abort(tcp_pcb);
        tcp_pcb = NULL;
    }
}


