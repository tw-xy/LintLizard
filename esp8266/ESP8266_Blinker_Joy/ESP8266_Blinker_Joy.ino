/* ------------------------------------------------------------------
 * ESP8266-01S + Blinker 虚拟摇杆  ->  串口发给 CH32V307
 *
 * 接线：见本仓库 docs/接线表.md
 *   ESP-01S TX  ->  CH32V307 PB11  (USART3_RX, J4 第 3 脚)
 *   ESP-01S RX  <-  CH32V307 PB10  (USART3_TX, J4 第 1 脚)
 *   ESP-01S GND <-> CH32V307 GND   (J4 第 7 脚 / J3 的 GND)
 *   ESP-01S 3V3 <-  独立 3.3V(>=500mA)，必须与板子共地
 *   ESP-01S EN  ->  3.3V  ← 必须接！悬空模块不启动（蓝灯是数据灯，不是电源灯）
 *
 * 通信协议：115200 8N1，每 20ms 发送一行
 *   {"x":-100,"y":-100}      x = 左负右正, y = 后退负/前进正
 *   换行符 '\n' 作为一帧结束
 *
 * 开发板设置（Arduino IDE -> 工具）：
 *   Board        : Generic ESP8266 Module
 *   Flash Size   : 1MB (FS:none OTA:~502KB)
 *   Crystal      : 26 MHz
 *   CPU Freq     : 80 MHz
 *   上传用的 USB-TTL 波特率随便，115200 即可
 * 需要的库：Blinker  (https://github.com/blinker-iot/blinker-library)
 * ------------------------------------------------------------------ */

#define BLINKER_WIFI
#include <Blinker.h>

/* ==================== 1. 账号信息放在 secrets.h 里（不进 git） ====================
 * 把同目录的 secrets.h.example 复制一份、改名成 secrets.h，填上自己的：
 *     auth = Blinker APP 里这台设备的 Secret Key
 *     ssid = 2.4GHz 的 WiFi 名（ESP8266 不支持 5GHz）
 *     pswd = WiFi 密码
 * secrets.h 已被 .gitignore 忽略，不会被提交到仓库。
 */
#include "secrets.h"

/* ==================== 2. 一般不用改 ==================== */
char joyKey[] = "joy";        // APP 里“摇杆”组件的键名，必须与这里一致

#define SEND_PERIOD_MS   20   // 20ms 发一帧 = 50Hz
#define LINK_TIMEOUT_MS 1000  // 超过 1s 没收到摇杆数据就自动回中（失效保护）
#define Y_INVERT          1   // 推“前”变成后退 -> 改成 1
#define X_INVERT          0   // 左右反了     -> 改成 1
#define TEST_SWEEP        0   // 改成 1 = 不连 WiFi，自动来回扫描（只测串口链路）

BlinkerJoystick JOY(joyKey);

static int8_t   g_x = 0, g_y = 0;          // -100 ~ +100
static uint32_t g_lastRx = 0, g_lastTx = 0;

/* 摇杆原始值 0~255（中位 128）-> -100 ~ +100 */
static int8_t axisToPct(uint8_t raw, uint8_t invert)
{
    int16_t c = (int16_t)raw - 128;
    if (invert) c = -c;
    int16_t p = (int32_t)c * 100 / 127;
    return (int8_t)constrain(p, -100, 100);
}

/* Blinker 摇杆回调：手机摇杆一动就会进来 */
void joystick_callback(uint8_t xAxis, uint8_t yAxis)
{
    g_x = axisToPct(xAxis, X_INVERT);
    g_y = axisToPct(yAxis, Y_INVERT);
    g_lastRx = millis();
}

void setup()
{
    Serial.begin(115200);                 // UART0 -> CH32V307 USART3
    g_lastRx = millis();
    g_lastTx = millis();

#if !TEST_SWEEP
    Blinker.begin(auth, ssid, pswd);
    JOY.attach(joystick_callback);
#endif
}

void loop()
{
#if !TEST_SWEEP
    Blinker.run();                        // 非阻塞，必须一直调用
    if (millis() - g_lastRx > LINK_TIMEOUT_MS) {
        g_x = 0;                          // 手机关了/断网 -> 回中停车
        g_y = 0;
    }
#else
    /* 简易扫描：x、y 从 -100 -> +100 -> -100 来回走，用于验证 UART 链路 */
    static int16_t sweep = 0;
    static int8_t  dir   = 1;
    if (millis() - g_lastRx > 100) {
        g_lastRx = millis();
        sweep += dir * 10;
        if (sweep >= 100) { sweep = 100; dir = -1; }
        if (sweep <= -100) { sweep = -100; dir = 1; }
        g_x = (int8_t)sweep;
        g_y = (int8_t)sweep;
    }
#endif

    /* 定周期发送，非阻塞 */
    if (millis() - g_lastTx >= SEND_PERIOD_MS) {
        g_lastTx = millis();
        Serial.printf("{\"x\":%d,\"y\":%d}\n", (int)g_x, (int)g_y);
    }
}
