#ifndef __BSP_TIME_H
#define __BSP_TIME_H

#include <stdint.h>

/* 1ms 时基（TIM2 中断），全工程唯一的时间源，不带任何死等 */
void     BSP_Time_Init(void);
uint32_t BSP_Millis(void);

/* 软定时器：到点了返回 1 并把 *lastMs 更新为当前时刻（非阻塞时间片用） */
uint8_t  BSP_Every(uint32_t *lastMs, uint32_t periodMs);

#endif /* __BSP_TIME_H */
