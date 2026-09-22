/********************************** (C) COPYRIGHT *******************************
* File Name          : app_roam.c
* Description        : 自动巡航特别版（非阻塞）
*                      上电 IDLE；推杆并回中后进入 FWD；
*                      前方 < STOP -> TURN，连续转向失败 -> BACK -> 反向 TURN；
*                      雷达/遥控链路失效 -> FAULT，停车。
*******************************************************************************/
#include "app_roam.h"
#include "app_config.h"
#include "app_radar.h"

#define ROAM_NO_DIST 0xFFFFu

static Roam_State_t s_state = ROAM_IDLE;
static uint32_t     s_stateMs = 0;
static uint32_t     s_manualUntilMs = 0;
static uint8_t      s_armed = 0;
static uint8_t      s_turnDir = 1;      /* 1 = 右，0 = 左 */
static uint8_t      s_turnAttempts = 0;

static int16_t iabs16(int16_t v)
{
    return (v < 0) ? (int16_t)(-v) : v;
}

static uint8_t manual_active(const Remote_State_t *rm)
{
    if(!rm->online)
    {
        return 0;
    }
    return (uint8_t)((iabs16(rm->x) > ROAM_MANUAL_THRESH ||
                      iabs16(rm->y) > ROAM_MANUAL_THRESH) ? 1u : 0u);
}

/*********************************************************************
 * @fn      choose_turn
 * @brief   选择转向方向：优先往空间大的一边；两边都没有有效距离时交替
 *********************************************************************/
static uint8_t choose_turn(uint16_t left, uint16_t right)
{
    if(left == ROAM_NO_DIST && right == ROAM_NO_DIST)
    {
        return (uint8_t)(s_turnDir ? 0u : 1u);
    }
    if(left == ROAM_NO_DIST)
    {
        return 0;                           /* 左边没有近障碍，向左 */
    }
    if(right == ROAM_NO_DIST)
    {
        return 1;                           /* 右边没有近障碍，向右 */
    }
    return (uint8_t)((right > left) ? 1u : 0u);
}

void Roam_Init(void)
{
    s_state = ROAM_IDLE;
    s_stateMs = 0;
    s_manualUntilMs = 0;
    s_armed = 0;
    s_turnDir = 1;
    s_turnAttempts = 0;
}

void Roam_Update(uint32_t nowMs, const Remote_State_t *rm,
                 int16_t *outX, int16_t *outY)
{
    uint16_t front;
    uint16_t left;
    uint16_t right;

    /* ---- 遥控链路断开：停车并解除武装，恢复后必须重新推杆 ---- */
    if(!rm->online)
    {
        s_state = ROAM_FAULT;
        s_armed = 0;
        *outX = 0;
        *outY = 0;
        return;
    }

    /* Web controls: releasing input must stay stopped; full reverse is reverse. */
    if(rm->manual)
    {
        s_armed = 0;
        s_state = (rm->x != 0 || rm->y != 0) ? ROAM_MANUAL : ROAM_IDLE;
        s_manualUntilMs = nowMs;
        *outX = rm->x;
        *outY = rm->y;
        return;
    }

    /* ---- 急停手势：摇杆向下拉到底 -> 解除自动并停车 ---- */
    if(rm->y <= ROAM_DISARM_Y && iabs16(rm->x) < ROAM_DISARM_X)
    {
        s_armed = 0;
        s_state = ROAM_IDLE;
        *outX = 0;
        *outY = 0;
        return;
    }

    /* ---- 手动接管：任意方向推杆超过阈值，立即手动；松手保持 1.5s ---- */
    if(manual_active(rm))
    {
        s_armed = 1;
        s_state = ROAM_MANUAL;
        s_manualUntilMs = nowMs + ROAM_MANUAL_HOLD_MS;
        *outX = rm->x;
        *outY = rm->y;
        return;
    }

    if(s_state == ROAM_MANUAL)
    {
        *outX = 0;
        *outY = 0;
        if((int32_t)(nowMs - s_manualUntilMs) < 0)
        {
            return;
        }
        s_state = ROAM_FWD;
        s_stateMs = nowMs;
        s_turnAttempts = 0;
    }

    if(!s_armed)
    {
        s_state = ROAM_IDLE;
        *outX = 0;
        *outY = 0;
        return;
    }

    if(!Radar_IsOnline(nowMs))
    {
        s_state = ROAM_FAULT;
        *outX = 0;
        *outY = 0;
        return;
    }

    front = Radar_GetFrontMm();
    left = Radar_GetLeftMm();
    right = Radar_GetRightMm();

    switch(s_state)
    {
        case ROAM_FWD:
            if(front != ROAM_NO_DIST && front < AVOID_STOP_MM)
            {
                /* 太近：停下来开始原地转向 */
                s_turnDir = choose_turn(left, right);
                s_state = ROAM_TURN;
                s_stateMs = nowMs;
                s_turnAttempts = 1;
                *outX = 0;
                *outY = 0;
            }
            else if(front != ROAM_NO_DIST && front < AVOID_SLOW_MM)
            {
                /* 中距：慢速前进 + 往空间大的一边绕 */
                *outY = ROAM_SLOW_PERMILLE;
                *outX = choose_turn(left, right) ? ROAM_SLOW_TURN_PERMILLE : -ROAM_SLOW_TURN_PERMILLE;
            }
            else
            {
                *outY = ROAM_FWD_PERMILLE;
                *outX = 0;
            }
            break;

        case ROAM_TURN:
            *outY = 0;
            *outX = s_turnDir ? ROAM_PIVOT_PERMILLE : -ROAM_PIVOT_PERMILLE;
            if((uint32_t)(nowMs - s_stateMs) >= ROAM_TURN_MS)
            {
                if(front == ROAM_NO_DIST || front > ROAM_CLEAR_MM)
                {
                    s_state = ROAM_FWD;
                    s_stateMs = nowMs;
                }
                else if(s_turnAttempts < ROAM_MAX_TURN_ATTEMPTS)
                {
                    s_turnDir = (uint8_t)(s_turnDir ? 0u : 1u);
                    s_turnAttempts++;
                    s_stateMs = nowMs;
                }
                else
                {
                    s_state = ROAM_BACK;
                    s_stateMs = nowMs;
                }
            }
            break;

        case ROAM_BACK:
            *outY = -ROAM_BACK_PERMILLE;
            *outX = s_turnDir ? -ROAM_SLOW_TURN_PERMILLE : ROAM_SLOW_TURN_PERMILLE;
            if((uint32_t)(nowMs - s_stateMs) >= ROAM_BACK_MS)
            {
                s_turnDir = (uint8_t)(s_turnDir ? 0u : 1u);
                s_state = ROAM_TURN;
                s_stateMs = nowMs;
                s_turnAttempts = 0;
            }
            break;

        case ROAM_IDLE:
        case ROAM_MANUAL:
        case ROAM_FAULT:
        default:
            s_state = ROAM_IDLE;
            *outX = 0;
            *outY = 0;
            break;
    }
}

Roam_State_t Roam_GetState(void)
{
    return s_state;
}

const char *Roam_StateName(Roam_State_t state)
{
    switch(state)
    {
        case ROAM_IDLE:   return "IDLE";
        case ROAM_MANUAL: return "MANUAL";
        case ROAM_FWD:    return "FWD";
        case ROAM_TURN:   return "TURN";
        case ROAM_BACK:   return "BACK";
        default:          return "FAULT";
    }
}
