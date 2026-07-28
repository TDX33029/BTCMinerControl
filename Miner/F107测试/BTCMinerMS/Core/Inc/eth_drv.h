#ifndef __ETH_DRV_H
#define __ETH_DRV_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

/* PHY address - DP83848 typically responds at addr 0x01 or 0x0C */
/* 注意: CubeMX HAL 配置中使用 LAN8742A_PHY_ADDRESS (0x01),
   与 DP83848 默认地址一致。更新晶振后重新生成时确认此值。 */
#define DP83848_PHY_ADDRESS   0x01U

typedef struct {
    uint8_t  mac[6];
    uint8_t  ip[4];
    uint8_t  gateway[4];
    uint8_t  subnet[4];
    uint16_t local_port;
} eth_config_t;

/* GPIO/PHY control - CubeMX doesn't config PHY RST PD2 */
void eth_reset_phy(void);       /* PD2 toggled PHY reset */
void eth_gpio_pd2_init(void);   /* CubeMX won't overwrite if named clearly */

/* PHY 调试 / 状态查询 */
void eth_print_phy_status(void);
int  eth_link_status(void);

/* ETH init/LwIP glue */
int  eth_init(const eth_config_t *cfg);
int  eth_connect(uint8_t dest_ip[4], uint16_t port);
int  eth_send(const uint8_t *data, uint16_t len);
int  eth_recv(uint8_t *buf, uint16_t buf_len);
int  eth_is_connected(void);
int  eth_has_ip(void);       /* true once the static IPv4 address is active */
void eth_poll(void);

#endif
