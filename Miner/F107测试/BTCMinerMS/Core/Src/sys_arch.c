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
#endif /* NO_SYS */

