#include "Delay.h"

void Delay_us(uint32_t xus)
{
#if defined(STM32F40_41xxx)
    SysTick->LOAD = 168 * xus;             /* F4 @ 168MHz */
#elif defined(STM32F10X_CL) || defined(STM32F10X_HD) || defined(STM32F10X_MD)
    SysTick->LOAD = 72 * xus;              /* F1 @ 72MHz */
#else
    SysTick->LOAD = 72 * xus;              /* default F1 @ 72MHz */
#endif
    SysTick->VAL = 0x00;
    SysTick->CTRL = 0x00000005;
    while(!(SysTick->CTRL & 0x00010000));
    SysTick->CTRL = 0x00000004;
}

void Delay_ms(uint32_t xms)
{
    while(xms--) {
        Delay_us(1000);
    }
}

void Delay_s(uint32_t xs)
{
    while(xs--) {
        Delay_ms(1000);
    }
}
