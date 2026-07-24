#ifndef __ETH_NETIF_H__
#define __ETH_NETIF_H__

#include "lwip/netif.h"

err_t ethernetif_init(struct netif *netif);
void  ethernetif_input(struct netif *netif);
void  ethernetif_poll_rx(void);
void  ethernetif_set_link(struct netif *netif, int up);
uint32_t eth_netif_get_rx_count(void);
uint32_t eth_netif_get_tx_count(void);
void     eth_netif_reset_counts(void);

#endif /* __ETH_NETIF_H__ */
