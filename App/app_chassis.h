#ifndef __APP_CHASSIS_H
#define __APP_CHASSIS_H

#include <stdint.h>

/*********************************************************************
 * 差速底盘：摇杆 (x, y) -> 左右两侧目标速度
 *
 *      左轮 = y + x
 *      右轮 = y - x
 *
 * 支持：直线前进/后退、左右转弯、原地打转（y=0, x=±100）
 * 输出单位千分比 ±1000‰，将来直接映射到电调/电机 PWM
 *********************************************************************/

typedef struct
{
    int16_t left;          /* -1000 ~ +1000 ‰ */
    int16_t right;
    int16_t x;             /* 实际参与解算的输入（已过死区） */
    int16_t y;
} Chassis_Output_t;

void                    Chassis_Init(void);
void                    Chassis_SetInput(int16_t x, int16_t y);   /* -100 ~ +100 */
void                    Chassis_Update(uint32_t nowMs);           /* 10ms 周期调用 */
const Chassis_Output_t *Chassis_Get(void);
uint8_t                 Chassis_IsFailsafe(void);                 /* 1 = 超时保护中 */

#endif /* __APP_CHASSIS_H */
