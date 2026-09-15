/********************************** (C) COPYRIGHT *******************************
* File Name          : app_cleaner.c
* Description        : 扫地执行机构（边刷 EN + 吸尘电调）
*                      车动就开、车停延时关；上电先让吸尘电调解锁（给 0 油门）
*******************************************************************************/
#include "app_cleaner.h"
#include "app_config.h"
#include "bsp_motor.h"
#include "ch32v30x.h"

static uint8_t  s_on = 0;
static uint8_t  s_armed = 0;
static uint32_t s_lastMoveMs = 0;

/* 边刷 EN：PE8 / PE10 */
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

void Cleaner_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    brush_en_write(0);
    s_on = 0;
    s_armed = 0;
    s_lastMoveMs = 0;

    /* 吸尘电调上电先给 0 油门，等它解锁（与驱动电调同一个 3 秒窗口） */
    Motor_SetVacuumPulse(VAC_OFF_PULSE_US);
}

void Cleaner_Task(uint32_t nowMs, uint8_t moving)
{
    uint8_t want;

    /* 上电头 MOTOR_ARM_DELAY_MS 内什么都不做，让电调解锁 */
    if(!s_armed)
    {
        if(nowMs < MOTOR_ARM_DELAY_MS)
        {
            return;
        }
        s_armed = 1;
        s_lastMoveMs = nowMs;
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
        brush_en_write(s_on);
        Motor_SetVacuumPulse(s_on ? VAC_ON_PULSE_US : VAC_OFF_PULSE_US);
    }
}

uint8_t Cleaner_IsOn(void)
{
    return s_on;
}

uint16_t Cleaner_GetVacuumPulse(void)
{
    return Motor_GetVacuumPulse();
}
