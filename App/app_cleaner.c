/********************************** (C) COPYRIGHT *******************************
* File Name          : app_cleaner.c
* Description        : 扫地执行机构（边刷 EN + 吸尘涡轮风机）
*                      车动就开、车停延时关；风机带缓启动/缓降（保护驱动和机械）
*******************************************************************************/
#include "app_cleaner.h"
#include "app_config.h"
#include "bsp_fan.h"
#include "ch32v30x.h"

static uint8_t  s_on = 0;
static uint8_t  s_armed = 0;
static uint32_t s_lastMoveMs = 0;

static uint8_t  s_fanDuty = 0;      /* 当前占空比 */
static uint8_t  s_fanTarget = 0;    /* 目标占空比（开 = FAN_ON_DUTY_PCT，关 = 0） */
static uint32_t s_tFanRamp = 0;

/* 边刷 EN：PE8 / PE10（两个 12V->5V 模块的高电平启动脚） */
static void brush_en_write(uint8_t on)
{
#if BRUSH_EN_ACTIVE_HIGH
    if(on)
    {
        GPIO_SetBits(GPIOE, GPIO_Pin_8 | GPIO_Pin_10);
    }
    else
    {
        GPIO_ResetBits(GPIOE, GPIO_Pin_8 | GPIO_Pin_10);
    }
#else
    if(on)
    {
        GPIO_ResetBits(GPIOE, GPIO_Pin_8 | GPIO_Pin_10);
    }
    else
    {
        GPIO_SetBits(GPIOE, GPIO_Pin_8 | GPIO_Pin_10);
    }
#endif
}

/*********************************************************************
 * @fn      fan_ramp
 * @brief   风机缓启动/缓降：每 stepMs 变 1%，从当前值逼近目标值
 *          升：FAN_ON_DUTY_PCT 个 1% 分摊 FAN_SOFT_START_MS
 *          降：同样分摊 FAN_SOFT_STOP_MS
 *********************************************************************/
static void fan_ramp(uint32_t nowMs)
{
    uint32_t stepMs;
    uint8_t  span = (FAN_ON_DUTY_PCT != 0u) ? FAN_ON_DUTY_PCT : 1u;

    if(s_fanDuty == s_fanTarget)
    {
        return;
    }

    if(s_fanDuty < s_fanTarget)
    {
        stepMs = (uint32_t)FAN_SOFT_START_MS / span;
    }
    else
    {
        stepMs = (uint32_t)FAN_SOFT_STOP_MS / span;
    }
    if(stepMs == 0u)
    {
        stepMs = 1u;
    }

    if((uint32_t)(nowMs - s_tFanRamp) >= stepMs)
    {
        s_tFanRamp = nowMs;
        if(s_fanDuty < s_fanTarget)
        {
            s_fanDuty++;
        }
        else
        {
            s_fanDuty--;
        }
        Fan_SetDuty(s_fanDuty);
    }
}

void Cleaner_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    brush_en_write(0);

    Fan_Init();                 /* 上电占空比 0%，风机不转 */

    s_on = 0;
    s_armed = 0;
    s_lastMoveMs = 0;
    s_fanDuty = 0;
    s_fanTarget = 0;
    s_tFanRamp = 0;
}

void Cleaner_Task(uint32_t nowMs, uint8_t moving)
{
    uint8_t want;

    /* 上电头 MOTOR_ARM_DELAY_MS 内不动，等驱动电调解锁完再谈扫地 */
    if(!s_armed)
    {
        if(nowMs < MOTOR_ARM_DELAY_MS)
        {
            return;
        }
        s_armed = 1;
        /* 视为"很久以前就停了"，否则上电瞬间会被当成"刚停下"而误开扫地 */
        s_lastMoveMs = (uint32_t)(nowMs - SWEEP_HOLD_MS);
        s_tFanRamp = nowMs;
    }

    if(moving)
    {
        s_lastMoveMs = nowMs;
    }

    /* 在动，或者刚停下还不到 SWEEP_HOLD_MS -> 开 */
    want = (uint8_t)((moving || ((uint32_t)(nowMs - s_lastMoveMs) < SWEEP_HOLD_MS)) ? 1 : 0);

    if(want != s_on)
    {
        s_on = want;
        brush_en_write(s_on);                       /* 边刷直接开关（模块自带软启动） */
        s_fanTarget = s_on ? (uint8_t)FAN_ON_DUTY_PCT : 0;
    }

    fan_ramp(nowMs);                                /* 风机按斜率走 */
}

uint8_t Cleaner_IsOn(void)
{
    return s_on;
}

uint8_t Cleaner_GetFanDuty(void)
{
    return s_fanDuty;
}
