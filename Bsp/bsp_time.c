/********************************** (C) COPYRIGHT *******************************
* File Name          : bsp_time.c
* Description        : 1ms 系统时基。TIM2 每 1ms 中断一次，主循环靠 BSP_Millis()
*                      分时间片，杜绝 Delay_Ms() 死等。
*******************************************************************************/
#include "bsp_time.h"
#include "app_config.h"
#include "ch32v30x.h"

static volatile uint32_t s_ms = 0;

void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/*********************************************************************
 * @fn      BSP_Time_Init
 * @brief   配置 TIM2 每 1ms 产生一次更新中断
 *********************************************************************/
void BSP_Time_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    NVIC_InitTypeDef        NVIC_InitStructure = {0};
    RCC_ClocksTypeDef       clocks;
    uint32_t                timClk, psc;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* 定时器时钟：APB1 分频不为 1 时 = 2 x PCLK1（CH32 与 STM32 同规则） */
    RCC_GetClocksFreq(&clocks);
    timClk = clocks.PCLK1_Frequency;
    if((RCC->CFGR0 & RCC_PPRE1) != RCC_PPRE1_DIV1)
    {
        timClk *= 2u;
    }
    psc = timClk / 10000u;              /* 计数频率 10kHz */
    if(psc == 0u)
    {
        psc = 1u;
    }

    TIM_TimeBaseInitStructure.TIM_Prescaler     = (uint16_t)(psc - 1u);
    TIM_TimeBaseInitStructure.TIM_Period        = (uint16_t)(10u - 1u);  /* 10 x 100us = 1ms */
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;   /* 时基优先级最高 */
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM2, ENABLE);
}

/*********************************************************************
 * @fn      BSP_Millis
 * @brief   上电以来的毫秒数（会回绕，比较请用 (now - last) >= period）
 *********************************************************************/
uint32_t BSP_Millis(void)
{
    return s_ms;
}

/*********************************************************************
 * @fn      BSP_Every
 * @brief   非阻塞软定时器
 *********************************************************************/
uint8_t BSP_Every(uint32_t *lastMs, uint32_t periodMs)
{
    uint32_t now = BSP_Millis();

    if((uint32_t)(now - *lastMs) >= periodMs)
    {
        *lastMs = now;
        return 1;
    }
    return 0;
}

/*********************************************************************
 * @fn      TIM2_IRQHandler
 * @brief   1ms 时基中断：只加一，越短越好
 *********************************************************************/
void TIM2_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        s_ms++;
    }
}
