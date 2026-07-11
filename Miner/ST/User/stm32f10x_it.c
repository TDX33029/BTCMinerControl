/**
 * STM32F10x 中断服务程序
 */

#include "stm32f10x_it.h"
#include "bm1366.h"

/* Cortex-M3 异常处理 */
void NMI_Handler(void) {}
void HardFault_Handler(void) { while(1); }
void MemManage_Handler(void) { while(1); }
void BusFault_Handler(void) { while(1); }
void UsageFault_Handler(void) { while(1); }
void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}

/* SysTick_Handler 在 main.c 中定义 */

/* USART1 IRQ: BM1366 数据接收 */
void USART1_IRQHandler(void) {
    bm1366_uart_isr_handler();
}
