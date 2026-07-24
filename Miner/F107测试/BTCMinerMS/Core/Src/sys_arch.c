/* ===== sys_arch.c -- bare-metal sys_arch for LwIP NO_SYS=1 ===== */
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "stm32f1xx_hal.h"

#if NO_SYS

u32_t sys_now(void) {
    return HAL_GetTick();
}

#endif /* NO_SYS */
