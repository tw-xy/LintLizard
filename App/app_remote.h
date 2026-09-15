#ifndef __APP_REMOTE_H
#define __APP_REMOTE_H

#include <stdint.h>

/*********************************************************************
 * 遥控链路层：把 ESP8266 发来的一行行 JSON 解析成摇杆值
 *
 * 协议（115200 8N1，'\n' 结尾）：
 *      {"x":-100,"y":100}
 *      x: -100(左) ~ +100(右)
 *      y: -100(后退) ~ +100(前进)
 * 以后要加字段随便加，解析器只找 x / y；非 '{' 开头的行直接忽略
 * （ESP8266 上电时 74880 波特的启动乱码就是这样被吃掉的）
 *********************************************************************/

typedef struct
{
    int16_t  x;              /* -100 ~ +100 */
    int16_t  y;              /* -100 ~ +100 */
    uint8_t  net;            /* 1 = ESP 报告自己已连上 WiFi/云端（协议里的 "net" 字段） */
    uint8_t  online;         /* 1 = 链路正常（超时时间内收到过有效帧） */
    uint32_t lastValidMs;    /* 最后一次有效帧的时间戳 */
    uint32_t frameCount;     /* 有效帧计数 */
    uint32_t errorCount;     /* 丢弃的行数（乱码 / 字段不全） */
} Remote_State_t;

void                   Remote_Init(void);
void                   Remote_Poll(void);   /* 主循环里反复调用，不阻塞 */
const Remote_State_t  *Remote_Get(void);

#endif /* __APP_REMOTE_H */
