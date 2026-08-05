#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/* ===== Bare-metal (NO OS) ===== */
#define NO_SYS                          1

/* ===== Memory ===== */
#define MEM_LIBC_MALLOC                 1
#define MEM_SIZE                        (10*1024)
#define MEMP_NUM_TCP_PCB                4
#define MEMP_NUM_TCP_SEG                16
#define MEMP_NUM_ARP_QUEUE              4
#define PBUF_POOL_SIZE                  8
#define PBUF_POOL_BUFSIZE               1524
#define PBUF_LINK_HLEN                  16

/* ===== UDP (required by DHCP) ===== */
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define TCP_TTL                         64
#define TCP_WND                         (4*TCP_MSS)
#define TCP_MSS                         1460
#define TCP_SND_BUF                     (4*TCP_MSS)
#define TCP_SND_QUEUELEN                (4*TCP_SND_BUF/TCP_MSS)

/* ===== IP ===== */
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define IP_FORWARD                      0
#define IP_REASSEMBLY                   0
#define IP_FRAG                         0

/* ===== ARP ===== */
#define LWIP_ARP                        1
#define ARP_TABLE_SIZE                  4
#define ARP_QUEUEING                    0

/* ===== ICMP (ping) ===== */
#define LWIP_ICMP                       1

/* ===== DHCP: IPv4 address/gateway/netmask acquired at runtime (needs UDP).
   If CubeMX regenerates this file, re-enable LWIP_UDP + LWIP_DHCP here. */
#define LWIP_DHCP                       1
#define LWIP_DHCP_DOES_ACD_CHECK        0   /* disable address-conflict detection:
   ACD probes the leased IP via ARP and can false-positive on routers with proxy
   ARP (or any ARP responder), causing a DHCPDECLINE that drops the lease
   immediately -- matches the "lease appears then instantly expires" symptom. */
#define LWIP_NETIF_HOSTNAME             1   /* send DHCP option 12 (hostname) */
#define LWIP_DNS                        0

/* ===== Stats off ===== */
#define LWIP_STATS                      0
#define LWIP_STATS_DISPLAY              0

/* ===== Raw API only (no socket) ===== */
#define LWIP_SOCKET                     0
#define LWIP_NETCONN                    0

/* ===== Timer ===== */
#define LWIP_TIMERS                     1
#define LWIP_TCPIP_CORE_LOCKING         0

/* ===== errno ===== */
#define LWIP_PROVIDE_ERRNO              1

/* ===== Random seed for TCP ISN ===== */
#include "stm32f1xx_hal.h"
#define LWIP_RAND()                     ((u32_t)HAL_GetTick())

/* Enable TCP keepalive probes (used by eth_drv.c tcp_client_connected).
   If CubeMX regenerates this file, re-add this line (or set it in CubeMX
   lwIP options) -- eth_drv.c compiles either way due to its #if guard. */
#define LWIP_TCP_KEEPALIVE              1

#endif /* __LWIPOPTS_H__ */



