/********************************** (C) COPYRIGHT *******************************
* File Name          : app_chassis.c
* Description        : 标准差速转向解算（死区 + 指数曲线 + 归一化 + 失控保护）
*******************************************************************************/
#include "app_chassis.h"
#include "app_config.h"
#include "bsp_time.h"

static Chassis_Output_t s_out;
static int16_t          s_x = 0;
static int16_t          s_y = 0;
static uint32_t         s_lastInputMs = 0;
static uint8_t          s_failsafe = 1;

static int32_t iabs32(int32_t v)
{
    return (v < 0) ? -v : v;
}

/*********************************************************************
 * @fn      apply_deadzone
 * @brief   摇杆中位附近的抖动直接归零
 *********************************************************************/
static int16_t apply_deadzone(int16_t v)
{
    if(iabs32(v) <= (int32_t)CHASSIS_DEADZONE)
    {
        return 0;
    }
    return v;
}

/*********************************************************************
 * @fn      apply_expo
 * @brief   指数曲线：小摇杆更细腻、大摇杆满输出
 *          （CHASSIS_EXPO_PCT == 0 时完全线性）
 *********************************************************************/
static int16_t apply_expo(int16_t v)
{
#if (CHASSIS_EXPO_PCT > 0)
    int32_t a = iabs32(v);
    int32_t shaped = a * (100 - CHASSIS_EXPO_PCT) / 100
                   + a * a * CHASSIS_EXPO_PCT / 10000;

    if(shaped > 100)
    {
        shaped = 100;
    }
    return (int16_t)((v < 0) ? -shaped : shaped);
#else
    return v;
#endif
}

void Chassis_Init(void)
{
    s_x = 0;
    s_y = 0;
    s_out.left  = 0;
    s_out.right = 0;
    s_out.x     = 0;
    s_out.y     = 0;
    s_lastInputMs = BSP_Millis();
    s_failsafe = 1;
}

void Chassis_SetInput(int16_t x, int16_t y)
{
    if(x > 100)
    {
        x = 100;
    }
    if(x < -100)
    {
        x = -100;
    }
    if(y > 100)
    {
        y = 100;
    }
    if(y < -100)
    {
        y = -100;
    }

    s_x = x;
    s_y = y;
    s_lastInputMs = BSP_Millis();
}

uint8_t Chassis_IsFailsafe(void)
{
    return s_failsafe;
}

const Chassis_Output_t *Chassis_Get(void)
{
    return &s_out;
}

/*********************************************************************
 * @fn      Chassis_Update
 * @brief   差速解算本体，10ms 调一次
 *
 *   1) 死区 + 指数曲线
 *   2) 标准差速混合： l = y + x , r = y - x
 *   3) 归一化：只要有一侧超过满量程，两边一起等比缩小，
 *      这样"前进100 + 右转100"不会被削掉一边、转向角不失真
 *   4) 换算成千分比输出（将来直接喂电调）
 *********************************************************************/
void Chassis_Update(uint32_t nowMs)
{
    int16_t x, y;
    int32_t l, r, maxAbs, scale = CHASSIS_MAX_PERMILLE;

    /* ---- 失控保护：这么久没新指令就归零 ---- */
    if((uint32_t)(nowMs - s_lastInputMs) > CHASSIS_INPUT_TIMEOUT_MS)
    {
        s_x = 0;
        s_y = 0;
        s_failsafe = 1;
    }
    else
    {
        s_failsafe = 0;
    }

    x = apply_expo(apply_deadzone(s_x));
    y = apply_expo(apply_deadzone(s_y));
    s_out.x = x;
    s_out.y = y;

    if(x == 0 && y == 0)
    {
        s_out.left  = 0;
        s_out.right = 0;
        return;
    }

    /* ---- 标准差速转向 ---- */
    l = (int32_t)y + (int32_t)x;
    r = (int32_t)y - (int32_t)x;

    /* ---- 归一化，保持转向比例 ---- */
    maxAbs = (iabs32(l) > iabs32(r)) ? iabs32(l) : iabs32(r);
    if(maxAbs > 100)
    {
        scale = scale * 100 / maxAbs;
    }

    l = l * scale / 100;
    r = r * scale / 100;

    if(CHASSIS_LEFT_INVERT)
    {
        l = -l;
    }
    if(CHASSIS_RIGHT_INVERT)
    {
        r = -r;
    }

    if(l > (int32_t)CHASSIS_MAX_PERMILLE)
    {
        l = (int32_t)CHASSIS_MAX_PERMILLE;
    }
    if(l < -(int32_t)CHASSIS_MAX_PERMILLE)
    {
        l = -(int32_t)CHASSIS_MAX_PERMILLE;
    }
    if(r > (int32_t)CHASSIS_MAX_PERMILLE)
    {
        r = (int32_t)CHASSIS_MAX_PERMILLE;
    }
    if(r < -(int32_t)CHASSIS_MAX_PERMILLE)
    {
        r = -(int32_t)CHASSIS_MAX_PERMILLE;
    }

    s_out.left  = (int16_t)l;
    s_out.right = (int16_t)r;
}
