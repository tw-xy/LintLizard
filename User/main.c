/********************************** (C) COPYRIGHT *******************************
* File Name          : main.c
* Description        : 履带小车 第一阶段 —— 娱乐与遥控
*
*   手机 Blinker 虚拟摇杆 -> ESP8266-01S -> USART3(PB10/PB11)
*        -> 解析 JSON -> 标准差速转向解算 -> USART1(PA9/PA10=COM4) 打印左右目标速度
*
*   本阶段不接电机，只验证协议与解算。架构为非阻塞时间片：
*       BSP_Millis() 给 1ms 时基，主循环按 10ms / 100ms / 1s 分片跑，
*       串口收发全部是中断 + 环形缓冲，没有任何 Delay_Ms() 死等。
*
*   文件分布：
*       Bsp/bsp_time.c    1ms 时基 + 软定时器
*       Bsp/bsp_uart.c    USART1 调试口 + USART3 ESP8266 口
*       App/app_remote.c  协议解析 + 链路超时
*       App/app_chassis.c 差速转向解算
*******************************************************************************/
#include "app_config.h"
#include "app_chassis.h"
#include "app_remote.h"
#include "bsp_motor.h"
#include "bsp_time.h"
#include "bsp_uart.h"
#include "ch32v30x.h"

int main(void)
{
    uint32_t tCtrl = 0, tMotor = 0, tDbg = 0, tStat = 0;
    uint32_t lastFrames = 0;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();

    BSP_Time_Init();
    BSP_Uart_Init();
    Remote_Init();
    Chassis_Init();
    Motor_Init();

    DBG_Printf("\r\n========================================\r\n");
    DBG_Printf(" CAR_REMOTE  CH32V307VCT6 @ %u Hz\r\n", (unsigned int)SystemCoreClock);
    DBG_Printf(" USART1 PA9/PA10 -> COM4  (debug, %u bps)\r\n", (unsigned int)DBG_UART_BAUD);
    DBG_Printf(" USART3 PB10/PB11 -> ESP8266-01S (%u bps)\r\n", (unsigned int)ESP_UART_BAUD);
    DBG_Printf(" ESC1 PA6(J4-29) ESC2 PA7(J4-31), %u Hz PWM\r\n",
               (unsigned int)(1000000u / MOTOR_PWM_PERIOD_US));
    DBG_Printf(" waiting joystick {\"x\":..,\"y\":..}\\n ...\r\n");
    DBG_Printf("========================================\r\n");

    while(1)
    {
        /* ---- 1) 收：把 ESP 的字节全部喂进协议解析器（随时可调，不阻塞） ---- */
        Remote_Poll();

        /* ---- 2) 10ms：差速解算时间片 ---- */
        if(BSP_Every(&tCtrl, TASK_CTRL_PERIOD_MS))
        {
            const Remote_State_t *rm = Remote_Get();

            if(rm->online)
            {
                Chassis_SetInput(rm->x, rm->y);
            }
            /* 掉线时不再喂新输入，Chassis 自己会在 CHASSIS_INPUT_TIMEOUT_MS 后归零 */
            Chassis_Update(BSP_Millis());
        }

        /* ---- 3) 20ms：把解算结果写到电调 PWM ---- */
        if(BSP_Every(&tMotor, TASK_MOTOR_PERIOD_MS))
        {
            const Chassis_Output_t *ch = Chassis_Get();

            if((BSP_Millis() < MOTOR_ARM_DELAY_MS) || Chassis_IsFailsafe())
            {
                /* 上电头 3 秒让电调自检/解锁；失控时输出中位（停） */
                Motor_SetNeutral();
            }
            else
            {
                Motor_SetPermille(ch->left, ch->right);
            }
        }

        /* ---- 4) 100ms：打印解算后的左右电机目标速度 ---- */
        if(BSP_Every(&tDbg, TASK_DBG_PERIOD_MS))
        {
            const Remote_State_t   *rm = Remote_Get();
            const Chassis_Output_t *ch = Chassis_Get();

            DBG_Printf("[cmd ] x=%+4d y=%+4d | L=%+5d R=%+5d | %4u/%4u us | %s\r\n",
                       rm->x, rm->y, ch->left, ch->right,
                       (unsigned int)Motor_GetPulseUs(0), (unsigned int)Motor_GetPulseUs(1),
                       Chassis_IsFailsafe() ? "FAILSAFE" : "ok");
        }

        /* ---- 5) 1s：统计行（帧率 / 丢帧 / 缓冲溢出） ---- */
        if(BSP_Every(&tStat, TASK_STAT_PERIOD_MS))
        {
            const Remote_State_t *rm = Remote_Get();
            uint32_t fps = rm->frameCount - lastFrames;

            lastFrames = rm->frameCount;
            DBG_Printf("[stat] %s frames=%u (%u/s) err=%u rx=%u rxovf=%u txdrop=%u\r\n",
                       rm->online ? "LINK-OK  " : "LINK-LOST",
                       (unsigned int)rm->frameCount, (unsigned int)fps,
                       (unsigned int)rm->errorCount,
                       (unsigned int)ESP_RxCount(),
                       (unsigned int)ESP_RxOverflow(),
                       (unsigned int)DBG_TxDropped());
        }
    }
}
