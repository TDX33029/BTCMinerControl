 #ifndef __DEBUG_SERIAL_H
 #define __DEBUG_SERIAL_H
 
 #include "stm32f4xx.h"
 
 /* 初始化调试串口 (USART6: PC6=TX, PC7=RX, 115200-8N1) */
 void DebugSerial_Init(void);
 
 /* 发送一个字符 */
 void DebugSerial_PutChar(char c);
 
 /* 发送字符串 */
 void DebugSerial_Puts(const char *str);
 
 /* 格式化打印 (简易版，支持 %s %d %u %x %f) */
 void DebugSerial_Printf(const char *fmt, ...);
 
 #endif
