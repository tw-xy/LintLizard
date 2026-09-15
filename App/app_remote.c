/********************************** (C) COPYRIGHT *******************************
* File Name          : app_remote.c
* Description        : ESP8266 遥控数据解析（非阻塞，逐字节喂进来组帧）
*******************************************************************************/
#include "app_remote.h"
#include "app_config.h"
#include "bsp_time.h"
#include "bsp_uart.h"

static Remote_State_t s_remote;
static uint8_t        s_line[REMOTE_RX_LINE_MAX];
static uint16_t       s_lineLen = 0;

static const char *skip_ws(const char *p)
{
    while(*p == ' ' || *p == '\t')
    {
        p++;
    }
    return p;
}

/*********************************************************************
 * @fn      parse_int
 * @brief   从字符串里取一个有符号整数
 *********************************************************************/
static int32_t parse_int(const char *p)
{
    int32_t v = 0;
    int     neg = 0;

    p = skip_ws(p);
    if(*p == '+' || *p == '-')
    {
        neg = (*p == '-');
        p++;
    }
    while(*p >= '0' && *p <= '9')
    {
        v = v * 10 + (int32_t)(*p - '0');
        if(v > 1000000)
        {
            break;                  /* 防跑飞 */
        }
        p++;
    }
    return neg ? -v : v;
}

/*********************************************************************
 * @fn      find_field
 * @brief   在 JSON 行里找 "key":<int>
 * @return  1 = 找到，0 = 没找到
 *********************************************************************/
static uint8_t find_field(const char *line, char key, int32_t *out)
{
    const char *p = line;

    while(*p != '\0')
    {
        if(*p == '"')
        {
            p++;
            if(*p == key)
            {
                p++;
                if(*p == '"')
                {
                    p++;
                    p = skip_ws(p);
                    if(*p == ':')
                    {
                        p++;
                        *out = parse_int(p);
                        return 1;
                    }
                }
            }
        }
        else
        {
            p++;
        }
    }
    return 0;
}

static int16_t clamp100(int32_t v)
{
    if(v > 100)
    {
        return 100;
    }
    if(v < -100)
    {
        return -100;
    }
    return (int16_t)v;
}

/*********************************************************************
 * @fn      parse_line
 * @brief   解析一行完整的 JSON
 *********************************************************************/
static void parse_line(uint8_t *line, uint16_t len)
{
    int32_t x = 0, y = 0;
    char   *p;

    line[len] = '\0';                     /* s_line 预留了结尾 0 */
    p = (char *)skip_ws((const char *)line);

    if(*p != '{')                         /* ESP 启动日志等杂行：丢掉 */
    {
        s_remote.errorCount++;
        return;
    }
    if(!find_field(p, 'x', &x) || !find_field(p, 'y', &y))
    {
        s_remote.errorCount++;
        return;
    }

    s_remote.x           = clamp100(x);
    s_remote.y           = clamp100(y);
    s_remote.lastValidMs = BSP_Millis();
    s_remote.frameCount++;
    s_remote.online      = 1;
}

/*********************************************************************
 * @fn      feed_byte
 * @brief   逐字节组帧：遇到 \n 或 \r 就交给 parse_line
 *********************************************************************/
static void feed_byte(uint8_t c)
{
    if(c == '\n' || c == '\r')
    {
        if(s_lineLen > 0u)
        {
            parse_line(s_line, s_lineLen);
            s_lineLen = 0;
        }
        return;
    }

    if(s_lineLen < (REMOTE_RX_LINE_MAX - 1u))
    {
        s_line[s_lineLen++] = c;
    }
    else
    {
        s_lineLen = 0;                    /* 一行太长，整行丢掉 */
        s_remote.errorCount++;
    }
}

void Remote_Init(void)
{
    s_remote.x           = 0;
    s_remote.y           = 0;
    s_remote.online      = 0;
    s_remote.lastValidMs = BSP_Millis();
    s_remote.frameCount  = 0;
    s_remote.errorCount  = 0;
    s_lineLen            = 0;
}

/*********************************************************************
 * @fn      Remote_Poll
 * @brief   把 ESP 口收到的字节全部搬出来解析；同时维护链路在线标志
 *********************************************************************/
void Remote_Poll(void)
{
    uint8_t  chunk[32];
    uint16_t n, i;

    while((n = ESP_Read(chunk, (uint16_t)sizeof(chunk))) > 0u)
    {
        for(i = 0; i < n; i++)
        {
            feed_byte(chunk[i]);
        }
    }

    if((uint32_t)(BSP_Millis() - s_remote.lastValidMs) > REMOTE_LINK_TIMEOUT_MS)
    {
        s_remote.online = 0;
    }
}

const Remote_State_t *Remote_Get(void)
{
    return &s_remote;
}
