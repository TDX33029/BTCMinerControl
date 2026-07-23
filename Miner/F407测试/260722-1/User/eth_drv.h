 #ifndef __ETH_DRV_H
 #define __ETH_DRV_H
 
 #if defined(STM32F40_41xxx)
#include "stm32f4xx.h"
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
#include "stm32f10x.h"
#else
#include "stm32f4xx.h"
#endif
 #include <stdint.h>
 
 typedef struct {
     uint8_t  mac[6];
     uint8_t  ip[4];
     uint8_t  gateway[4];
     uint8_t  subnet[4];
     uint16_t local_port;
 } eth_config_t;
 
 void eth_gpio_init(void);
 void eth_reset(void);
 int  eth_init(const eth_config_t *cfg);
 int  eth_connect(uint8_t dest_ip[4], uint16_t port);
 int  eth_send(const uint8_t *data, uint16_t len);
 int  eth_recv(uint8_t *buf, uint16_t buf_len);
 int  eth_is_connected(void);
 int  eth_is_disconnected(void);
 uint8_t eth_socket_status(void);
 void eth_close(void);
 void eth_poll(void);
 
 #endif
