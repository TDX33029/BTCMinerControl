#include "stm32f10x_it.h"
#include "bm1366.h"

extern volatile uint32_t g_ms;

void NMI_Handler(void) {}
void HardFault_Handler(void) { while (1); }
void MemManage_Handler(void) { while (1); }
void BusFault_Handler(void) { while (1); }
void UsageFault_Handler(void) { while (1); }
void DebugMon_Handler(void) {}
void SVC_Handler(void) {}
void PendSV_Handler(void) {}
void SysTick_Handler(void) { g_ms++; }
void USART1_IRQHandler(void) { bm1366_uart_isr_handler(); }
