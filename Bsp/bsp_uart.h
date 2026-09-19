#ifndef __BSP_UART_H
#define __BSP_UART_H

#include <stdint.h>

/*********************************************************************
 * USART1 = 调试口（板载 WCH-Link -> PC 的 COM4），非阻塞打印
 * USART3 = ESP8266 口（PB10/TX、PB11/RX），中断收发 + 环形缓冲
 *********************************************************************/

void     BSP_Uart_Init(void);

/* ---- 调试口 ---- */
void     DBG_Write(const uint8_t *data, uint16_t len);
void     DBG_Printf(const char *fmt, ...);
uint32_t DBG_TxDropped(void);

/* ---- ESP8266 口 ---- */
uint16_t ESP_Read(uint8_t *dst, uint16_t maxLen);
uint16_t ESP_Write(const uint8_t *src, uint16_t len);
uint32_t ESP_RxOverflow(void);
uint32_t ESP_RxCount(void);

/* ---- USART2：雷达口（PA2=TX / PA3=RX = J3 第 32 / 34 脚）---- */
uint16_t RADAR_Read(uint8_t *dst, uint16_t maxLen);
uint32_t RADAR_RxCount(void);
uint32_t RADAR_RxOverflow(void);

#endif /* __BSP_UART_H */
