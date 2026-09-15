/********************************** (C) COPYRIGHT *******************************
* File Name          : bsp_motor.c
* Description        : 履带底盘电调 PWM 输出
*                      TIM3_CH1(PA6) = 左电调, TIM3_CH2(PA7) = 右电调
*                      50Hz，脉宽 1000~2000us（中位 1500us）
*                      纯硬件 PWM，主循环只改比较值，不占用 CPU
*******************************************************************************/
#include "bsp_motor.h"
#include "app_config.h"
#include "ch32v30x.h"

#define MOTOR_TIM          TIM3
#define MOTOR_LEFT_CH      TIM_Channel_1     /* PA6 */
#define MOTOR_RIGHT_CH     TIM_Channel_2     /* PA7 */

static volatile uint16_t s_pulse[2] = {MOTOR_PULSE_NEUTRAL, MOTOR_PULSE_NEUTRAL};

/*********************************************************************
 * @fn      permille_to_pulse
 * @brief   ±1000‰ -> 1000~2000us
 *********************************************************************/
static uint16_t permille_to_pulse(int16_t permille)
{
    int32_t span = (int32_t)MOTOR_PULSE_MAX - (int32_t)MOTOR_PULSE_NEUTRAL;
    int32_t pulse = (int32_t)MOTOR_PULSE_NEUTRAL
                  + (int32_t)permille * span / 1000;

    if(pulse < (int32_t)MOTOR_PULSE_MIN)
    {
        pulse = (int32_t)MOTOR_PULSE_MIN;
    }
    if(pulse > (int32_t)MOTOR_PULSE_MAX)
    {
        pulse = (int32_t)MOTOR_PULSE_MAX;
    }
    return (uint16_t)pulse;
}

/*********************************************************************
 * @fn      Motor_Init
 * @brief   TIM3 CH1/CH2 输出 50Hz PWM，初始中位
 *********************************************************************/
void Motor_Init(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    TIM_OCInitTypeDef       TIM_OCInitStructure = {0};
    RCC_ClocksTypeDef       clocks;
    uint32_t                timClk, psc;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* PA6/PA7 复用推挽输出（TIM3 默认映射，不需要重映射） */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 计数时钟 1MHz -> 1 个计数 = 1us */
    RCC_GetClocksFreq(&clocks);
    timClk = clocks.PCLK1_Frequency;
    if((RCC->CFGR0 & RCC_PPRE1) != RCC_PPRE1_DIV1)
    {
        timClk *= 2u;
    }
    psc = timClk / 1000000u;
    if(psc == 0u)
    {
        psc = 1u;
    }

    TIM_TimeBaseInitStructure.TIM_Prescaler     = (uint16_t)(psc - 1u);
    TIM_TimeBaseInitStructure.TIM_Period        = (uint16_t)(MOTOR_PWM_PERIOD_US - 1u);
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(MOTOR_TIM, &TIM_TimeBaseInitStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = MOTOR_PULSE_NEUTRAL;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC1Init(MOTOR_TIM, &TIM_OCInitStructure);
    TIM_OC2Init(MOTOR_TIM, &TIM_OCInitStructure);

    TIM_OC1PreloadConfig(MOTOR_TIM, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(MOTOR_TIM, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(MOTOR_TIM, ENABLE);

    s_pulse[0] = MOTOR_PULSE_NEUTRAL;
    s_pulse[1] = MOTOR_PULSE_NEUTRAL;
    TIM_SetCompare1(MOTOR_TIM, MOTOR_PULSE_NEUTRAL);
    TIM_SetCompare2(MOTOR_TIM, MOTOR_PULSE_NEUTRAL);

    TIM_Cmd(MOTOR_TIM, ENABLE);
}

/*********************************************************************
 * @fn      Motor_SetPermille
 * @brief   把底盘解算出来的 ±1000‰ 写成两路电调脉宽
 *********************************************************************/
void Motor_SetPermille(int16_t left, int16_t right)
{
    uint16_t pulseL, pulseR;

    if(MOTOR_LEFT_INVERT)
    {
        left = (int16_t)(-left);
    }
    if(MOTOR_RIGHT_INVERT)
    {
        right = (int16_t)(-right);
    }

    pulseL = permille_to_pulse(left);
    pulseR = permille_to_pulse(right);

    s_pulse[0] = pulseL;
    s_pulse[1] = pulseR;
    TIM_SetCompare1(MOTOR_TIM, pulseL);
    TIM_SetCompare2(MOTOR_TIM, pulseR);
}

/*********************************************************************
 * @fn      Motor_SetNeutral
 * @brief   两路都回中位（电调停 / 上电自检时用）
 *********************************************************************/
void Motor_SetNeutral(void)
{
    s_pulse[0] = MOTOR_PULSE_NEUTRAL;
    s_pulse[1] = MOTOR_PULSE_NEUTRAL;
    TIM_SetCompare1(MOTOR_TIM, MOTOR_PULSE_NEUTRAL);
    TIM_SetCompare2(MOTOR_TIM, MOTOR_PULSE_NEUTRAL);
}

uint16_t Motor_GetPulseUs(uint8_t idx)
{
    return s_pulse[idx & 1u];
}
