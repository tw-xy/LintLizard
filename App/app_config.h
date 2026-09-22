/********************************** (C) COPYRIGHT *******************************
* File Name          : app_config.h
* Description        : 履带小车遥控工程 —— 全部可调参数集中在这里
*                      第一阶段：ESP8266(Blinker 虚拟摇杆) -> USART3 -> 差速解算 -> USART1 打印
*******************************************************************************/
#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

/* ==================== 串口 ==================== */
#define DBG_UART_BAUD           115200u  /* USART1(PA9/PA10) = 板载 WCH-Link = PC 的 COM4 */
#define ESP_UART_BAUD           115200u  /* USART3(PB10/PB11) = ESP8266-01S */
#define RADAR_UART_BAUD         115200u  /* USART2(PA2/PA3) = 激光雷达（X2 兼容）*/

/* ==================== 遥控链路 ==================== */
#define REMOTE_LINK_TIMEOUT_MS   300u    /* 超过这么久没收到有效帧 -> 判定链路断开 */
#define REMOTE_RX_LINE_MAX        64u    /* 一行 JSON 最长字节数（超出丢行） */

/* ==================== 差速底盘 ==================== */
#define CHASSIS_DEADZONE           4     /* 摇杆死区（百分比，0~20） */
#define CHASSIS_EXPO_PCT           0     /* 指数曲线强度 0~100，0 = 完全线性 */
#define CHASSIS_MAX_PERMILLE    1000     /* 输出满量程 ±1000‰(=100%)，将来直接喂电调 */
#define CHASSIS_INPUT_TIMEOUT_MS 300u    /* 兜底：这么久没有新指令 -> 输出 0 + FAILSAFE */
#define CHASSIS_LEFT_INVERT        0     /* 某侧电机装反了改成 1 */
#define CHASSIS_RIGHT_INVERT       0

/* ==================== 电机 / 电调 ==================== */
/* 标准 RC 有刷电调：50Hz，1000us=满倒车，1500us=中位(停)，2000us=满前进 */
#define MOTOR_PWM_PERIOD_US      20000u  /* 20ms = 50Hz */
#define MOTOR_PULSE_MIN           1000u
#define MOTOR_PULSE_NEUTRAL       1500u
#define MOTOR_PULSE_MAX           2000u
#define MOTOR_ARM_DELAY_MS        3000u  /* 上电后先保持中位这么久，让电调自检/解锁 */
#define MOTOR_LEFT_INVERT            0   /* 某个轮子转向反了改 1（信号极性反过来） */
#define MOTOR_RIGHT_INVERT           0

/* ==================== 时间片周期 ==================== */
#define TASK_CTRL_PERIOD_MS       10u    /* 10ms 控制周期 = 100Hz */
#define TASK_MOTOR_PERIOD_MS      20u    /* 20ms 刷新电调（与 50Hz 帧同步） */
#define TASK_DBG_PERIOD_MS       100u    /* 100ms 打印一次解算结果 */
#define TASK_STAT_PERIOD_MS     1000u    /* 1s 打印一次统计 */
#define TASK_UI_PERIOD_MS        100u    /* 100ms 刷新一次 OLED 界面 */

/* ==================== OLED / 界面 ==================== */
#define UI_CONNECTED_HOLD_MS    3000u    /* "已连接"停留多久后切到方向箭头 */
#define UI_SPINNER_STEP_MS       150u    /* 转圈图标每格停留多久 */

/* ==================== 激光雷达嗅探 ==================== */
#define RADAR_STAT_PERIOD_MS     500u    /* 每隔多久打一行雷达摘要 */
#define RADAR_STREAM_ENABLE        1u    /* USART3 -> Pi: R,angle_cdeg,dist_mm\n */
#define RADAR_STREAM_DECIMATE      8u    /* 每 8 个有效点发 1 个；最大约 10KB/s */

/* ==================== 激光雷达避障 ==================== */
/* 第一版先保持 0：只解析点云并打印前向距离，不接管电机。
 * 距离/方向验证正确后改成 1，烧录即可启用前进限速/停车。 */
#define AVOID_ENABLE                   1u

/* 雷达安装角度：X2 的 0° 是雷达电机正前方。装车后如果 0° 没朝车头，
 * 用这个偏移修正。单位：0.01°，例如 90° = 9000。 */
#define AVOID_FRONT_OFFSET_CDEG        0

#define AVOID_FRONT_HALF_ANGLE_CDEG 2500  /* 前向扇区半角：±25.00° */
#define AVOID_SIDE_HALF_ANGLE_CDEG  2500  /* 左/右扇区半角：±25.00° */

#define AVOID_STOP_MM                300u /* 小于这个距离：禁止继续前进 */
#define AVOID_SLOW_MM                600u /* 这个距离内：前进速度线性降低 */
#define AVOID_MIN_POINTS               3u /* 一个扇区至少这么多有效点才认 */
#define AVOID_WINDOW_MS              200u /* 点云最小距离保持窗口 */
#define AVOID_TIMEOUT_MS             300u /* 超过这么久没新包：雷达失效 */

#define AVOID_MIN_VALID_MM           120u /* X2 最小测距 */
#define AVOID_MAX_VALID_MM          8000u /* X2 最大测距 */

/* ==================== 自动巡航特别版 ==================== */
/* 上电不直接跑；推摇杆并回中后才进入自动巡航。
 * 自动模式仅依赖雷达前/左/右扇区，速度故意压低，便于测试安全。 */
#define ROAM_ENABLE                   1u
#define ROAM_FWD_PERMILLE           400   /* 前方畅通时前进速度 */
#define ROAM_SLOW_PERMILLE          200   /* 前方 300~600mm 时的前进分量 */
#define ROAM_SLOW_TURN_PERMILLE     350   /* 慢速绕障时的转向分量 */
#define ROAM_PIVOT_PERMILLE         500   /* 原地转向分量 */
#define ROAM_BACK_PERMILLE          300   /* 后退脱困速度 */
#define ROAM_TURN_MS                700u  /* 每次转向持续时间 */
#define ROAM_BACK_MS                500u  /* 连续两次转向仍被挡时后退时间 */
#define ROAM_CLEAR_MM               600u  /* 前方大于此值视为畅通 */
#define ROAM_MAX_TURN_ATTEMPTS         2u /* 同一障碍最多尝试两次转向 */
#define ROAM_MANUAL_THRESH            25  /* 摇杆超过这个值视为手动接管 */
#define ROAM_MANUAL_HOLD_MS         1500u /* 松手后多久恢复自动 */
#define ROAM_DISARM_Y                -80  /* 摇杆向下拉到底：解除自动并停车 */
#define ROAM_DISARM_X                 30  /* 解除手势要求 X 接近中位 */

/* ==================== 扫地执行机构（边刷 + 涡轮风机）====================
 * 边刷：两个 12V->5V 降压模块，EN 高电平 -> 输出 5V（PE8 / PE10）
 * 风机：内置驱动的涡轮风机（4 线 VCC/GND/PWM/FG），18kHz 占空比调速
 *       PWM = PB8 = TIM4_CH3（J4 第 25 脚）；0% = 停，100% = 最高速
 * 逻辑：底盘一动就开（风机缓启动），车停 SWEEP_HOLD_MS 之后缓降关掉
 */
#define SWEEP_HOLD_MS           2000u    /* 车停后延时多久关边刷/风机 */
#define BRUSH_EN_ACTIVE_HIGH       1     /* 边刷 EN 高电平有效 */
#define FAN_PWM_FREQ_HZ         18000u   /* 风机调速频率（规格书 ~18kHz） */
#define FAN_ON_DUTY_PCT            50u   /* 目标占空比（先 50%，实测后再调） */
#define FAN_SOFT_START_MS        1500u   /* 缓启动：0% -> 目标用多久 */
#define FAN_SOFT_STOP_MS          800u   /* 缓降：目标 -> 0% 用多久 */

#endif /* __APP_CONFIG_H */
