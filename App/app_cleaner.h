#ifndef __APP_CLEANER_H
#define __APP_CLEANER_H

#include <stdint.h>

/*********************************************************************
 * 扫地执行机构：边刷（两个 12V->5V 降压模块的 EN）+ 吸尘涡轮风机
 *   边刷 EN = PE8 / PE10（J3 第 43 / 45 脚，高电平开）
 *   风机 PWM = PB8 = TIM4_CH3（J4 第 25 脚，18kHz 占空比，0%=停）
 *   逻辑：底盘一动就开（风机缓启动），车停 SWEEP_HOLD_MS 之后缓降关掉
 *********************************************************************/

void    Cleaner_Init(void);

/* 每个 TASK_MOTOR_PERIOD_MS 调一次；moving = 底盘是否在动 */
void    Cleaner_Task(uint32_t nowMs, uint8_t moving);

uint8_t Cleaner_IsOn(void);        /* 1 = 边刷通电 / 风机在转 */
uint8_t Cleaner_GetFanDuty(void);  /* 当前风机占空比 %，给日志用 */

#endif /* __APP_CLEANER_H */
