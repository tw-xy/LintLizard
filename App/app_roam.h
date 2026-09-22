#ifndef __APP_ROAM_H
#define __APP_ROAM_H

#include <stdint.h>
#include "app_remote.h"

/*********************************************************************
 * 自动巡航特别版：
 *   上电先停在 IDLE，推一下摇杆再回中 -> 进入自动巡航；
 *   自动模式只依赖雷达前/左/右扇区：前方空就走，遇障就转向/后退；
 *   推摇杆可随时手动接管，松手 1.5s 后恢复自动；
 *   雷达失效或遥控链路断开 -> 停车/等待重新武装。
 *   网页 manual=1：直接手动输出，回中保持停止，完整后退不触发巡航急停手势。
 *********************************************************************/

typedef enum
{
    ROAM_IDLE = 0,
    ROAM_MANUAL,
    ROAM_FWD,
    ROAM_TURN,
    ROAM_BACK,
    ROAM_FAULT
} Roam_State_t;

void         Roam_Init(void);
void         Roam_Update(uint32_t nowMs, const Remote_State_t *rm,
                         int16_t *outX, int16_t *outY);

Roam_State_t Roam_GetState(void);
const char  *Roam_StateName(Roam_State_t state);

#endif /* __APP_ROAM_H */
