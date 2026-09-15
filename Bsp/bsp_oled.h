#ifndef __BSP_OLED_H
#define __BSP_OLED_H

#include <stdint.h>

/*********************************************************************
 * 0.96" SSD1306 128x64 OLED（4 线 I2C）
 *   SCL = PB6 = J4 第 22 脚
 *   SDA = PB7 = J4 第 20 脚
 *   软 I2C（GPIO 模拟开漏），不占用硬件 I2C 外设；地址一般 0x3C
 *********************************************************************/

#define OLED_W   128
#define OLED_H    64

/* 0 = 找到屏幕并初始化成功；1 = 没找到（总线没有应答） */
uint8_t OLED_Init(void);
uint8_t OLED_IsReady(void);

/* ---- 画到帧缓冲（不产生 I2C 流量） ---- */
void OLED_Clear(void);
void OLED_SetPixel(int16_t x, int16_t y, uint8_t on);
void OLED_HLine(int16_t x, int16_t y, int16_t w, uint8_t on);
void OLED_VLine(int16_t x, int16_t y, int16_t h, uint8_t on);
void OLED_Line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t on);
void OLED_Rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t on);
void OLED_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t on);

/* 画字模：16 高，宽 8 或 16，列优先按页；y 必须是 8 的倍数 */
void OLED_DrawGlyph16(int16_t x, int16_t y, const uint8_t *data, uint8_t w);

/* 贴一张单色位图：行优先、每字节 8 个水平像素、MSB 在左（app_glyphs.h 的格式） */
void OLED_BlitMono(int16_t x, int16_t y, uint8_t w, uint8_t h, const uint8_t *data);

/* ---- 刷到屏幕 ---- */
uint8_t OLED_Flush(void);       /* 整屏一次刷完（阻塞 ~90ms，只在初始化时用） */
uint8_t OLED_FlushPart(uint8_t firstPage, uint8_t pageCount);   /* 只刷部分页，非阻塞用 */

#endif /* __BSP_OLED_H */
