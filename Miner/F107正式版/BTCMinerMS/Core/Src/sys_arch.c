/* ===== sys_arch.c -- bare-metal sys_arch for LwIP NO_SYS=1 ===== */
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "stm32f1xx_hal.h"

#if NO_SYS

u32_t sys_now(void) {
    return HAL_GetTick();
}


/* Critical section protection for NO_SYS mode (polling, no ISR touches LwIP) */
sys_prot_t sys_arch_protect(void) { return 0; }
void sys_arch_unprotect(sys_prot_t p) { (void)p; }

/* ===== UDP stubs (UDP disabled for miner TCP-only protocol) ===== */
#if !LWIP_UDP
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
void udp_init(void) { }
void udp_input(struct pbuf *p, struct netif *inp) { (void)p; (void)inp; }
void udp_netif_ip_addr_changed(const ip_addr_t* old_addr, const ip_addr_t* new_addr) { (void)old_addr; (void)new_addr; }
#endif
#endif /* NO_SYS */


