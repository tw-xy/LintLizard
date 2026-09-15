/********************************** (C) COPYRIGHT *******************************
* File Name          : ch32v30x_it.c
* Description        : 异常处理。外设中断（TIM2 / USART1 / USART3）分别放在
*                      bsp_time.c / bsp_uart.c 里，便于模块化维护。
*******************************************************************************/
#include "ch32v30x_it.h"

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void NMI_Handler(void)
{
    while(1)
    {
    }
}

void HardFault_Handler(void)
{
    NVIC_SystemReset();
    while(1)
    {
    }
}
