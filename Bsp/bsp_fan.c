/********************************** (C) COPYRIGHT *******************************
* File Name          : bsp_fan.c
* Description        : 吸尘涡轮风机调速（18kHz PWM，占空比控制）
*                      PB8 = TIM4_CH3 = J4 第 25 脚
*                      与驱动电调分开用 TIM4，避免和 TIM3 的 50Hz 冲突
*******************************************************************************/
#include "bsp_fan.h"
#include "app_config.h"
#include "ch32v30x.h"

#define FAN_TIM     TIM4
#define FAN_CH      TIM_Channel_3        /* PB8 */

static volatile uint8_t  s_duty = 0;
static uint16_t          s_arr = 0;

static uint16_t duty_to_ccr(uint8_t pct)
{
    if(pct > 100u)
    {
        pct = 100u;
    }
    return (uint16_t)(((uint32_t)pct * ((uint32_t)s_arr + 1u)) / 100u);
}

/*********************************************************************
 * @fn      Fan_Init
 * @brief   TIM4_CH3 输出 ~18kHz PWM，上电占空比 0（风机停）
 *********************************************************************/
void Fan_Init(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    TIM_OCInitTypeDef       TIM_OCInitStructure = {0};
    RCC_ClocksTypeDef       clocks;
    uint32_t                timClk, arr;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    /* PB8 = TIM4_CH3（默认映射，不需要重映射） */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_8;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* 定时器时钟（APB1 分频不为 1 时 = 2×PCLK1） */
    RCC_GetClocksFreq(&clocks);
    timClk = clocks.PCLK1_Frequency;
    if((RCC->CFGR0 & RCC_PPRE1) != RCC_PPRE1_DIV1)
    {
        timClk *= 2u;
    }

    /* 96MHz / 18kHz = 5333 -> ARR = 5332（占空比分辨率 1/5333，足够细） */
    arr = timClk / FAN_PWM_FREQ_HZ;
    if(arr == 0u)
    {
        arr = 1u;
    }
    s_arr = (uint16_t)(arr - 1u);

    TIM_TimeBaseInitStructure.TIM_Prescaler     = 0;
    TIM_TimeBaseInitStructure.TIM_Period        = s_arr;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(FAN_TIM, &TIM_TimeBaseInitStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = 0;         /* 0% = 停 */
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC3Init(FAN_TIM, &TIM_OCInitStructure);

    TIM_OC3PreloadConfig(FAN_TIM, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(FAN_TIM, ENABLE);
    TIM_SetCompare3(FAN_TIM, 0);
    TIM_Cmd(FAN_TIM, ENABLE);

    s_duty = 0;
}

void Fan_SetDuty(uint8_t percent)
{
    if(percent > 100u)
    {
        percent = 100u;
    }
    s_duty = percent;
    TIM_SetCompare3(FAN_TIM, duty_to_ccr(percent));
}

uint8_t Fan_GetDuty(void)
{
    return s_duty;
}
