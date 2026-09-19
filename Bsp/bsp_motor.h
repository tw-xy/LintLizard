#ifndef __BSP_MOTOR_H
#define __BSP_MOTOR_H

#include <stdint.h>

/*********************************************************************
 * 两个有刷电调（履带左右各一个）
 *   信号：标准 RC 信号 50Hz / 20ms 周期
 *         1000us = 满倒车，1500us = 中位(停)，2000us = 满前进
 *   引脚：PA6 = TIM3_CH1 = J4 第 29 脚 (丝印 D4) -> 左电调
 *         PA7 = TIM3_CH2 = J4 第 31 脚 (丝印 D3) -> 右电调
 *********************************************************************/

void     Motor_Init(void);
void     Motor_SetPermille(int16_t left, int16_t right);   /* ±1000‰ */
void     Motor_SetNeutral(void);                           /* 两路都回中位 */
uint16_t Motor_GetPulseUs(uint8_t idx);                    /* idx: 0=左 1=右 */

#endif /* __BSP_MOTOR_H */
