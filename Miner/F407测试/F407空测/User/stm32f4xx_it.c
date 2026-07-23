/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Main Interrupt Service Routines for BTCMinerControl F407
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_it.h"
#include "bm1366.h"

/* External variables --------------------------------------------------------*/
extern volatile uint32_t g_ms;

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
  while (1) {}
}

void MemManage_Handler(void)
{
  while (1) {}
}

void BusFault_Handler(void)
{
  while (1) {}
}

void UsageFault_Handler(void)
{
  while (1) {}
}

void DebugMon_Handler(void)
{
}

void SVC_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/******************************************************************************/

void USART1_IRQHandler(void)
{
  bm1366_uart_isr_handler();
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
