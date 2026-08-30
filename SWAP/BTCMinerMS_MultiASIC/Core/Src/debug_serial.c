/* HAL adapted: USART2 on PD5/PD6 */
#include "main.h"
#include <stdio.h>

/* Extern UART handle from main.c */
extern UART_HandleTypeDef huart2;

int fputc(int ch, FILE *f) {
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
