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
#include "app_ui.h"
#include "app_cleaner.h"
#include "bsp_motor.h"
#include "bsp_oled.h"
#include "bsp_time.h"
#include "bsp_uart.h"
#include "ch32v30x.h"

int main(void)
{
    uint32_t tCtrl = 0, tMotor = 0, tDbg = 0, tStat = 0, tUi = 0;
    uint32_t lastFrames = 0;
    Ui_Screen_t lastScreen = (Ui_Screen_t)0xFF;
    Ui_Dir_t    lastDir = (Ui_Dir_t)0xFF;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();

    BSP_Time_Init();
    BSP_Uart_Init();
    Remote_Init();
    Chassis_Init();
    Motor_Init();
    Cleaner_Init();

    DBG_Printf("\r\n========================================\r\n");
    DBG_Printf(" CAR_REMOTE  CH32V307VCT6 @ %u Hz\r\n", (unsigned int)SystemCoreClock);
    DBG_Printf(" USART1 PA9/PA10 -> COM4  (debug, %u bps)\r\n", (unsigned int)DBG_UART_BAUD);
    DBG_Printf(" USART3 PB10/PB11 -> ESP8266-01S (%u bps)\r\n", (unsigned int)ESP_UART_BAUD);
    DBG_Printf(" ESC1 PA6(J4-29) ESC2 PA7(J4-31), %u Hz PWM\r\n",
               (unsigned int)(1000000u / MOTOR_PWM_PERIOD_US));
    DBG_Printf(" waiting joystick {\"x\":..,\"y\":..}\\n ...\r\n");
    DBG_Printf("========================================\r\n");

    App_Ui_Init();                       /* OLED: 软件I2C(PB6/PB7) + 扫描 + 初始化 */
    DBG_Printf("[ui  ] OLED SSD1306 ready=%u\r\n", (unsigned int)OLED_IsReady());

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
            uint8_t moving;

            if((BSP_Millis() < MOTOR_ARM_DELAY_MS) || Chassis_IsFailsafe())
            {
                /* 上电头 3 秒让电调自检/解锁；失控时输出中位（停） */
                Motor_SetNeutral();
                moving = 0;
            }
            else
            {
                Motor_SetPermille(ch->left, ch->right);
                moving = (uint8_t)((ch->left != 0 || ch->right != 0) ? 1 : 0);
            }

            /* 扫地执行机构：车动 -> 边刷通电 + 吸尘 1250us；车停 2 秒后关 */
            Cleaner_Task(BSP_Millis(), moving);
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

        /* ---- 5) 100ms：OLED 界面（无网络转圈 / 已连接 / 方向箭头） ---- */
        if(BSP_Every(&tUi, TASK_UI_PERIOD_MS))
        {
            App_Ui_Update(BSP_Millis());

            /* 界面内容变化时打一行日志，不看屏也能验证逻辑 */
            if((App_Ui_GetScreen() != lastScreen) || (App_Ui_GetDirection() != lastDir))
            {
                const Remote_State_t *rm = Remote_Get();

                lastScreen = App_Ui_GetScreen();
                lastDir    = App_Ui_GetDirection();
                DBG_Printf("[ui  ] screen=%-9s dir=%-10s oled=%u net=%u online=%u\r\n",
                           App_Ui_ScreenName(lastScreen), App_Ui_DirName(lastDir),
                           (unsigned int)OLED_IsReady(),
                           (unsigned int)rm->net, (unsigned int)rm->online);
            }
        }

        /* ---- 6) 1s：统计行（帧率 / 丢帧 / OLED / 界面状态） ---- */
        if(BSP_Every(&tStat, TASK_STAT_PERIOD_MS))
        {
            const Remote_State_t *rm = Remote_Get();
            uint32_t fps = rm->frameCount - lastFrames;

            lastFrames = rm->frameCount;
            DBG_Printf("[stat] %s frames=%u (%u/s) err=%u rxovf=%u | ui=%-9s dir=%-10s oled=%u net=%u | clean=%u fan=%u%%\r\n",
                       rm->online ? "LINK-OK  " : "LINK-LOST",
                       (unsigned int)rm->frameCount, (unsigned int)fps,
                       (unsigned int)rm->errorCount,
                       (unsigned int)ESP_RxOverflow(),
                       App_Ui_ScreenName(App_Ui_GetScreen()),
                       App_Ui_DirName(App_Ui_GetDirection()),
                       (unsigned int)OLED_IsReady(),
                       (unsigned int)rm->net,
                       (unsigned int)Cleaner_IsOn(),
                       (unsigned int)Cleaner_GetFanDuty());
        }
    }
}
