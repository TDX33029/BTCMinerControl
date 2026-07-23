#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/* ===== NO OS ===== */
#define NO_SYS                  1
#define LWIP_NETCONN            0
#define LWIP_SOCKET             0

/* ===== Memory ===== */
#define MEM_LIBC_MALLOC         0
#define MEM_ALIGNMENT           4
#define MEM_SIZE                (30 * 1024)
#define MEMP_NUM_PBUF           20
#define MEMP_NUM_TCP_PCB        8
#define MEMP_NUM_TCP_SEG        64
#define MEMP_NUM_SYS_TIMEOUT    8
#define PBUF_POOL_SIZE          20
#define PBUF_POOL_BUFSIZE       1520
#define PBUF_LINK_HLEN          16
#define MEM_USE_POOLS           0
#define MEMP_USE_CUSTOM_POOLS   0

/* ===== TCP ===== */
#define LWIP_TCP                1
#define TCP_TTL                 255
#define TCP_WND                 (4 * TCP_MSS)
#define TCP_MSS                 1460
#define TCP_SND_BUF             (4 * TCP_MSS)
#define TCP_SND_QUEUELEN        (4 * TCP_SND_BUF / TCP_MSS)
#define TCP_LISTEN_BACKLOG      1
#define TCP_OVERSIZE            TCP_MSS

/* ===== UDP ===== */
#define LWIP_UDP                0

/* ===== IP / ARP ===== */
#define LWIP_IPV4               1
#define LWIP_IPV6               0
#define IP_REASSEMBLY           0
#define IP_FRAG                 0
#define LWIP_ARP                1
#define ARP_TABLE_SIZE          8
#define ARP_QUEUEING            1
#define LWIP_ETHARP             1
#define LWIP_ETHERNET           1

/* ===== ICMP ===== */
#define LWIP_ICMP               0
#define LWIP_BROADCAST_PING     0
#define LWIP_MULTICAST_PING     0

/* ===== DHCP / AutoIP ===== */
#define LWIP_DHCP               0
#define LWIP_AUTOIP             0
#define LWIP_DHCP_AUTOIP_COOP   0

/* ===== DNS ===== */
#define LWIP_DNS                0

/* ===== IGMP ===== */
#define LWIP_IGMP               0

/* ===== ALTCP (TLS) ===== */
#define LWIP_ALTCP              0
#define LWIP_ALTCP_TLS          0

/* ===== IPv6 ===== */
#define LWIP_IPV6               0
#define LWIP_IPV6_FRAG          0
#define LWIP_IPV6_REASS         0
#define LWIP_ICMP6              0
#define LWIP_IPV6_MLD           0
#define LWIP_IPV6_DHCP6         0
#define LWIP_IPV6_AUTOCONFIG    0

/* ===== Multicast ===== */
#define LWIP_MULTICAST_TX_OPTIONS 0

/* ===== Statistics ===== */
#define LWIP_STATS              0
#define LWIP_STATS_DISPLAY      0
#define LWIP_PROVIDE_ERRNO      1

/* ===== Loopback ===== */
#define LWIP_NETIF_LOOPBACK     0

/* ===== Timers ===== */
#define LWIP_TIMERS             1
#define LWIP_EVENT_API          0
#define LWIP_CALLBACK_API       1

/* ===== SNMP ===== */
#define LWIP_SNMP               0
#define MIB2_STATS              0

/* ===== PPP ===== */
#define PPP_SUPPORT             0

/* ===== 6LoWPAN ===== */
#define LWIP_6LOWPAN            0

/* ===== Other ===== */
#define LWIP_RAW                1
#define LWIP_NETIF_HOSTNAME     0
#define LWIP_NETIF_STATUS_CALLBACK 0
#define LWIP_NETIF_LINK_CALLBACK   0
#define LWIP_NETIF_REMOVE_CALLBACK 0
#define LWIP_NETIF_HWADDRHINT   0
#define LWIP_SO_RCVTIMEO        0
#define LWIP_SO_SNDTIMEO        0
#define LWIP_SO_RCVBUF          0
#define LWIP_TCP_KEEPALIVE      0
#define LWIP_TCP_TIMESTAMPS     0
#define LWIP_TCP_WND_UPDATE_THRESHOLD  (TCP_WND / 4)

#endif
