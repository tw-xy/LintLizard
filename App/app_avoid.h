#ifndef __APP_AVOID_H
#define __APP_AVOID_H

#include <stdint.h>

/*********************************************************************
 * 前向避障仲裁：
 *   遥控输入 + 雷达前向扇区距离 -> 输出给差速底盘
 *   非阻塞，只在 10ms 控制时间片里调用
 *
 *   AVOID_ENABLE=0 时只更新状态并原样透传；
 *   AVOID_ENABLE=1 时才会限制/停止前进。
 *********************************************************************/

typedef enum
{
    AVOID_CLEAR = 0,
    AVOID_SLOW,
    AVOID_STOP,
    AVOID_FAULT
} Avoid_State_t;

void         Avoid_Init(void);
void         Avoid_Apply(int16_t inX, int16_t inY, uint32_t nowMs,
                         int16_t *outX, int16_t *outY);

Avoid_State_t Avoid_GetState(void);
const char  *Avoid_StateName(Avoid_State_t state);
uint16_t     Avoid_GetFrontMm(void);

#endif /* __APP_AVOID_H */
