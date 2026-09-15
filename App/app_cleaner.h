#ifndef __APP_CLEANER_H
#define __APP_CLEANER_H

#include <stdint.h>

/*********************************************************************
 * 扫地执行机构：边刷（两个 12V->5V 降压模块的 EN）+ 吸尘电调
 *   逻辑：底盘一动就开，车停 SWEEP_HOLD_MS 之后才关
 *   引脚：边刷 EN = PE8 / PE10（J3 第 43 / 45 脚，高电平开）
 *         吸尘电调 = PB0 = TIM3_CH3（J4 第 17 脚）
 *********************************************************************/

void     Cleaner_Init(void);

/* 每个 TASK_MOTOR_PERIOD_MS 调一次；moving = 底盘是否在动 */
void     Cleaner_Task(uint32_t nowMs, uint8_t moving);

uint8_t  Cleaner_IsOn(void);
uint16_t Cleaner_GetVacuumPulse(void);

#endif /* __APP_CLEANER_H */
