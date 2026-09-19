#ifndef __APP_RADAR_H
#define __APP_RADAR_H

#include <stdint.h>

/*********************************************************************
 * 激光雷达"嗅探"：USART2(PA3) 收雷达的字节流，每 0.5 秒在 COM4 打一行摘要
 *   用来验证：雷达有没有输出、波特率对不对、包头/角度是否正常
 *   接线：雷达 TXD -> PA3 (J3 第 34 脚)，GND 共地，VCC 5V，第4脚(M_CTR) 接 3.3V
 *********************************************************************/

void     Radar_Init(void);
void     Radar_Task(uint32_t nowMs);     /* 主循环里反复调用 */

uint32_t Radar_GetBytes(void);
uint32_t Radar_GetPackets(void);

#endif /* __APP_RADAR_H */
