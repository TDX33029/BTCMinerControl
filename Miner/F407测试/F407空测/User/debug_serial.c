/**
 * 调试串口 — USART6 @ PC6(TX) PC7(RX), 115200 8N1
 * 用于输出运行状态和错误信息
 */

#include "debug_serial.h"
#include <stdarg.h>
#include <stdio.h>

void DebugSerial_Init(void) {
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;

    /* USART6 在 APB2 总线 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART6, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

    /* PC6 = USART6 TX (AF8), PC7 = USART6 RX (AF8) */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource6, GPIO_AF_USART6);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource7, GPIO_AF_USART6);

    gpio.GPIO_Pin   = GPIO_Pin_6;
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &gpio);

    gpio.GPIO_Pin   = GPIO_Pin_7;
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &gpio);

    USART_StructInit(&usart);
    usart.USART_BaudRate = 115200;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART6, &usart);
    USART_Cmd(USART6, ENABLE);
}

void DebugSerial_PutChar(char c) {
    while (USART_GetFlagStatus(USART6, USART_FLAG_TXE) == RESET);
    USART_SendData(USART6, (uint8_t)c);
}

void DebugSerial_Puts(const char *str) {
    while (*str) {
        if (*str == '\n') DebugSerial_PutChar('\r');
        DebugSerial_PutChar(*str++);
    }
}

void DebugSerial_Printf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    DebugSerial_Puts(buf);
}