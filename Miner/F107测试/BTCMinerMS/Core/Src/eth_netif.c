/* ===== eth_netif.c -- LwIP netif driver for STM32F107 ETH with DP83848 ===== */
#include "eth_netif.h"
#include "lwip_eth.h"
#include "lwip/pbuf.h"
#include "lwip/err.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include <string.h>
#include <stdio.h>
#include "lwip/stats.h"

/* ===== DMA Descriptors & Buffers (owned by netif layer) ===== */
#define ETH_RX_DESC_COUNT   4
#define ETH_TX_DESC_COUNT   1   /* single TX desc w/ self-loop chain; simpler + robust */
#define ETH_BUF_SIZE        1520

static ETH_DMADESCTypeDef  eth_rx_desc[ETH_RX_DESC_COUNT];
static ETH_DMADESCTypeDef  eth_tx_desc[ETH_TX_DESC_COUNT];
static uint8_t             eth_rx_buff[ETH_RX_DESC_COUNT][ETH_BUF_SIZE];
static uint8_t             eth_tx_buff[ETH_TX_DESC_COUNT][ETH_BUF_SIZE];
static uint32_t            dbg_rx_count = 0;
static uint32_t            dbg_tx_count = 0;

/* ===== Forward declaration for LwIP low_level functions ===== */
static err_t   low_level_output(struct netif *netif, struct pbuf *p);
static err_t   low_level_init(struct netif *netif);

/* ===== Public: init DMA descriptors ===== */
void ethernetif_dma_init(void) {
    ETH_DMA_Init(eth_rx_desc, eth_rx_buff[0], ETH_RX_DESC_COUNT,
                 eth_tx_desc, eth_tx_buff[0], ETH_TX_DESC_COUNT,
                 ETH_BUF_SIZE, ETH_BUF_SIZE);
}

/* ===== Public: set link status on netif ===== */
void ethernetif_set_link(struct netif *netif, int up) {
    if (up)
        netif_set_link_up(netif);
    else
        netif_set_link_down(netif);
}

/* ===== LwIP netif init function ===== */
err_t ethernetif_init(struct netif *netif) {
    netif->name[0] = 'e';
    netif->name[1] = '0';
    netif->output     = etharp_output;
    netif->linkoutput = low_level_output;
    netif->mtu        = 1500;
    netif->flags      = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    netif->hwaddr_len = ETH_HWADDR_LEN;

    low_level_init(netif);
    return ERR_OK;
}

/* ===== low_level_init: configure MAC address from netif ===== */
static err_t low_level_init(struct netif *netif) {
    /* MAC address is already set via MACA0HR/MACA0LR by ETH_MAC_Init().
       Copy from netif->hwaddr so LwIP sees it. */
    return ERR_OK;
}

/* ===== Low-level output: send a pbuf via ETH TX DMA ===== */
static err_t low_level_output(struct netif *netif, struct pbuf *p) {
    struct pbuf *q;
    uint8_t *dst;

    /* Single TX descriptor with a self-loop chain (ETH_TX_DESC_COUNT=1):
       after sending desc0 the DMA follows Buffer2NextDescAddr back to desc0,
       sees OWN=0 and suspends (TBUS) until we re-arm it. This matches the
       original desc0-reuse behavior but with TCH on the correct bit (20). */
    static uint32_t tx_idx = 0;
    ETH_DMADESCTypeDef *d = &eth_tx_desc[tx_idx];

    /* Wait for TX descriptor to be available (OWN bit cleared by DMA) */
    uint32_t timeout = 1000000;
    while (d->Status & ETH_DMATxDesc_OWN) {
        if (!--timeout) {
            printf("[ETH] TX desc %lu stuck (OWN)\r\n", (unsigned long)tx_idx);
            return ERR_TIMEOUT;
        }
    }

    dst = (uint8_t *)(d->Buffer1Addr);
    uint32_t offset = 0;

    /* Copy pbuf chain into TX buffer */
    for (q = p; q != NULL; q = q->next) {
        if (offset + q->len > ETH_BUF_SIZE) {
            printf("[ETH] TX buffer overflow\r\n");
            return ERR_BUF;
        }
        memcpy(dst + offset, q->payload, q->len);
        offset += q->len;
    }

    /* Program TX descriptor: TBS1 = byte count, then hand to DMA */
    d->ControlBufferSize = (offset & ETH_DMATxDesc_TBS1);
    d->Status = ETH_DMATxDesc_OWN | ETH_DMATxDesc_FS | ETH_DMATxDesc_LS | ETH_DMATxDesc_IC | ETH_DMATxDesc_TCH;

    /* Advance so the next frame uses the next descriptor the DMA will reach */
    tx_idx = (tx_idx + 1) % ETH_TX_DESC_COUNT;

    /* Request transmission */
    ETH_DMATransmissionRequest();
    dbg_tx_count++;

    /* One-shot TX diagnostic: did the DMA actually pick up the descriptor
       (clear OWN) after we armed + polled it? own=0 -> DMA transmitted;
       own=1 -> DMA never sent it. DMASR TPS=6 means "suspended, descriptor
       unavailable", TPS=0 means "stopped". RBUS bit 7 = RX buffer unavail. */
    static int diag_left = 3;
    if (diag_left > 0) {
        diag_left--;
        uint32_t spins = 0;
        while ((d->Status & ETH_DMATxDesc_OWN) && ++spins < 100000);
        uint32_t dmasr = ETH->DMASR;
        int own = (d->Status & ETH_DMATxDesc_OWN) ? 1 : 0;
        int armed = (d == &eth_tx_desc[0]) ? 0 : 1;
        printf("[TXD] armed=%d own_now=%d(0=sent) spins=%lu DMASR=0x%08lX TPS=%lu TBUS(bit2)=%lu\r\n",
               armed, own, (unsigned long)spins, (unsigned long)dmasr,
               (unsigned long)((dmasr >> 20) & 7UL),
               (unsigned long)((dmasr >> 2) & 1UL));
    }
    return ERR_OK;
}

/* ===== Low-level input: receive a pbuf from ETH RX DMA ===== */
static struct pbuf *low_level_input(void) {
    uint32_t rx_count = ETH_RX_DESC_COUNT;
    uint32_t rx_buf_size = ETH_BUF_SIZE;
    struct pbuf *p, *q;
    uint16_t len;
    int i;

    for (i = 0; i < (int)rx_count; i++) {
        ETH_DMADESCTypeDef *d = &eth_rx_desc[i];

        /* Skip if not yet received (OWN bit still set = DMA owns it) */
        if (d->Status & ETH_DMARxDesc_OWN) continue;

        len = ETH_GetRxPktSize(d);
        if (len == 0 || len > rx_buf_size) {
            ETH_ReleaseRxDesc(d, eth_rx_buff[i], rx_count, rx_buf_size);
            continue;
        }

        /* Allocate pbuf */
        p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
        if (p == NULL) {
            printf("[ETH] No pbuf for RX %d bytes\r\n", len);
            ETH_ReleaseRxDesc(d, eth_rx_buff[i], rx_count, rx_buf_size);
            continue;
        }

        /* Copy data from RX buffer into pbuf chain */
        uint32_t copy_offset = 0;
        for (q = p; q != NULL; q = q->next) {
            uint32_t copy_len = (copy_offset + q->len <= len) ? q->len : (len - copy_offset);
            memcpy(q->payload, eth_rx_buff[i] + copy_offset, copy_len);
            copy_offset += copy_len;
        }

        /* Release RX descriptor back to DMA */
        ETH_ReleaseRxDesc(d, eth_rx_buff[i], rx_count, rx_buf_size);

        /* Return first received frame */
        dbg_rx_count++;
        return p;
    }
    return NULL;
}

/* ===== Poll RX (called from eth_poll) ===== */
void ethernetif_poll_rx(void) {
    /* This is called from eth_poll(); the actual packet feeding is done
       in ethernetif_input() which is called separately. */
}

/* ===== ethernetif_input: process received frames via LwIP ===== */
void ethernetif_input(struct netif *netif) {
    struct pbuf *p;

    while ((p = low_level_input()) != NULL) {
        /* Feed to LwIP */
        if (netif->input(p, netif) != ERR_OK) {
            pbuf_free(p);
        }
    }
}
/* ===== Debug: read/reset frame counters ===== */
uint32_t eth_netif_get_rx_count(void) { return dbg_rx_count; }
uint32_t eth_netif_get_tx_count(void) { return dbg_tx_count; }
void eth_netif_reset_counts(void) { dbg_rx_count = 0; dbg_tx_count = 0; }
