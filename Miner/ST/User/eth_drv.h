/**
 * 浠ュお缃戦┍鍔?鈥?STM32F107 + DP83848 PHY + LwIP
 *
 * 浣跨敤 STM32 鍐呯疆 MAC (ETH) + RMII 鎺ュ彛澶栨帴 DP83848 PHY銆?
 * LwIP 鍗忚鏍堟彁渚?TCP/IP (raw API, 瑁告満鏃?RTOS)銆?
 *
 * 纭欢杩炴帴 (RMII):
 *   MDIO:   PC1=MDC, PA2=MDIO
 *   鏃堕挓:   PA1=REF_CLK (50MHz 鈫?25MHz for DP83848 via internal divider)
 *   鏁版嵁:   PB12=TX_EN, PB13=TX_D0, PB15=TX_D1
 *           PC4=RX_D0, PC5=RX_D1, PA0=CRS_DV, PA7=RX_CLK
 *   澶嶄綅:   PD2=ETH_RST
 */

#ifndef __ETH_DRV_H
#define __ETH_DRV_H

#include "stm32f10x.h"
#include <stdint.h>

/* 缃戠粶閰嶇疆缁撴瀯浣?(淇濇寔鍘?W5500 鎺ュ彛鍏煎) */
typedef struct {
    uint8_t  mac[6];
    uint8_t  ip[4];
    uint8_t  gateway[4];
    uint8_t  subnet[4];
    uint16_t local_port;
} eth_config_t;

/* 鍏紑鍑芥暟 (涓庡師 W5500 API 瀹屽叏涓€鑷? */
void eth_gpio_init(void);    /* 鍒濆鍖?ETH RMII 寮曡剼 */
void eth_reset(void);       /* 澶嶄綅 PHY */
int  eth_init(const eth_config_t *cfg);  /* 鍒濆鍖?MAC+PHY+LwIP */
int  eth_connect(uint8_t dest_ip[4], uint16_t port);
int  eth_send(const uint8_t *data, uint16_t len);
int  eth_recv(uint8_t *buf, uint16_t buf_len);
int  eth_is_connected(void);
int  eth_is_disconnected(void);
uint8_t eth_socket_status(void);
void eth_close(void);
void eth_poll(void);        /* LwIP 椹卞姩 + ETH 涓柇澶勭悊 */

#endif
