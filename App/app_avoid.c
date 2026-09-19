/********************************** (C) COPYRIGHT *******************************
* File Name          : app_avoid.c
* Description        : 前向避障仲裁（非阻塞）
*                      前方 < STOP：禁止继续前进（允许倒车/原地转）
*                      前方 < SLOW：前进速度线性降低
*                      雷达失效：禁止继续前进（允许倒车）
*******************************************************************************/
#include "app_avoid.h"
#include "app_config.h"
#include "app_radar.h"

#define AVOID_NO_DIST 0xFFFFu

static Avoid_State_t s_state = AVOID_FAULT;
static uint16_t      s_frontMm = AVOID_NO_DIST;

void Avoid_Init(void)
{
    s_state = AVOID_FAULT;
    s_frontMm = AVOID_NO_DIST;
}

void Avoid_Apply(int16_t inX, int16_t inY, uint32_t nowMs,
                 int16_t *outX, int16_t *outY)
{
    int16_t  ox = inX;
    int16_t  oy = inY;
    uint8_t  online;
    uint16_t front;

    online = Radar_IsOnline(nowMs);
    front = Radar_GetFrontMm();
    s_frontMm = front;

    if(!online || front == AVOID_NO_DIST)
    {
        s_state = AVOID_FAULT;
    }
    else if(front < AVOID_STOP_MM)
    {
        s_state = AVOID_STOP;
    }
    else if(front < AVOID_SLOW_MM)
    {
        s_state = AVOID_SLOW;
    }
    else
    {
        s_state = AVOID_CLEAR;
    }

#if AVOID_ENABLE
    if(s_state == AVOID_FAULT || s_state == AVOID_STOP)
    {
        /* 只禁止继续前进；倒车和原地转向仍允许，便于脱离 */
        if(oy > 0)
        {
            oy = 0;
        }
    }
    else if(s_state == AVOID_SLOW && oy > 0)
    {
        int32_t span = (int32_t)AVOID_SLOW_MM - (int32_t)AVOID_STOP_MM;

        if(span <= 0)
        {
            oy = 0;
        }
        else
        {
            oy = (int16_t)(((int32_t)oy * ((int32_t)front - (int32_t)AVOID_STOP_MM)) / span);
        }
    }
#else
    (void)online;
#endif

    *outX = ox;
    *outY = oy;
}

Avoid_State_t Avoid_GetState(void)
{
    return s_state;
}

const char *Avoid_StateName(Avoid_State_t state)
{
    switch(state)
    {
        case AVOID_CLEAR: return "CLEAR";
        case AVOID_SLOW:  return "SLOW";
        case AVOID_STOP:  return "STOP";
        default:          return "FAULT";
    }
}

uint16_t Avoid_GetFrontMm(void)
{
    return s_frontMm;
}
