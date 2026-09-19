#ifndef __APP_RADAR_H
#define __APP_RADAR_H

#include <stdint.h>

/*********************************************************************
 * YDLIDAR X2 兼容雷达解析：
 *   USART2(PA3) 收字节流 -> 校验包 -> 距离/角度 -> 前/左/右扇区最小距离
 *   接线：雷达 TXD -> PA3 (J3 第 34 脚)，GND 共地，VCC 5V，M_CTR 接 3.3V
 *
 *   X2 点云包格式（小端）：
 *     AA 55 | CT | LSN | FSA(2) | LSA(2) | S1(2) ... Sn(2) | CS(2)
 *   CS = 前 8 字节与每个采样点按 16 位逐字异或
 *   距离 = 采样字 >> 2，单位 mm
 *********************************************************************/

void     Radar_Init(void);
void     Radar_Task(uint32_t nowMs);     /* 主循环里反复调用 */

/* 有效包数 / 接收字节数 / 校验错误数 */
uint32_t Radar_GetBytes(void);
uint32_t Radar_GetPackets(void);
uint32_t Radar_GetChecksumErrors(void);

/* 最近一个避障窗口内的扇区最小距离；0xFFFF = 当前没有有效点 */
uint16_t Radar_GetFrontMm(void);
uint16_t Radar_GetLeftMm(void);
uint16_t Radar_GetRightMm(void);

/* 雷达是否在 AVOID_TIMEOUT_MS 内产生过有效包 */
uint8_t  Radar_IsOnline(uint32_t nowMs);

#endif /* __APP_RADAR_H */
