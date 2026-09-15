#ifndef __APP_UI_H
#define __APP_UI_H

#include <stdint.h>

/*********************************************************************
 * OLED 界面（0.96" SSD1306 128x64，软件 I2C：PB6=SCL / PB7=SDA）
 *
 *   上电            -> 无网络 + 转圈（等 ESP 报"已联网"）
 *   ESP 报已联网    -> 已连接（停留 UI_CONNECTED_HOLD_MS）
 *   之后            -> 运动方向箭头（8 方向 + 停止）
 *   中途断网        -> 退回"无网络 + 转圈"
 *
 * 数据来源：app_remote（net 字段 / x,y）+ app_chassis（解算后的 x,y）
 *********************************************************************/

typedef enum
{
    UI_SCREEN_CONNECTING = 0,   /* 无网络 + 转圈 */
    UI_SCREEN_CONNECTED,        /* 已连接 */
    UI_SCREEN_DIRECTION         /* 方向箭头 */
} Ui_Screen_t;

typedef enum
{
    UI_DIR_STOP = 0,
    UI_DIR_FWD,      /* 前 */
    UI_DIR_BACK,     /* 后 */
    UI_DIR_LEFT,     /* 左 */
    UI_DIR_RIGHT,    /* 右 */
    UI_DIR_FL,       /* 前左 */
    UI_DIR_FR,       /* 前右 */
    UI_DIR_BL,       /* 后左 */
    UI_DIR_BR        /* 后右 */
} Ui_Dir_t;

void        App_Ui_Init(void);
void        App_Ui_Update(uint32_t nowMs);   /* 每个 TASK_UI_PERIOD_MS 调一次 */
Ui_Screen_t App_Ui_GetScreen(void);
Ui_Dir_t    App_Ui_GetDirection(void);
const char *App_Ui_ScreenName(Ui_Screen_t s);   /* 给日志用 */
const char *App_Ui_DirName(Ui_Dir_t d);

#endif /* __APP_UI_H */
