#ifndef __BSP_FAN_H
#define __BSP_FAN_H

#include <stdint.h>

/*********************************************************************
 * 吸尘涡轮风机（内置驱动，4 线：VCC / GND / PWM / FG）
 *   VCC 12V（10~15V，正负不能接反），FG 测速不接
 *   PWM：~18kHz 占空比调速，0% = 停，100% = 最高速
 *   引脚：PB8 = TIM4_CH3 = J4 第 25 脚
 *********************************************************************/

void    Fan_Init(void);
void    Fan_SetDuty(uint8_t percent);    /* 0 ~ 100 */
uint8_t Fan_GetDuty(void);

#endif /* __BSP_FAN_H */
