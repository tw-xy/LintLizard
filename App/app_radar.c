/********************************** (C) COPYRIGHT *******************************
* File Name          : app_radar.c
* Description        : 激光雷达数据嗅探（YDLIDAR X2 兼容协议）
*                      包头后依次是 CT(包类型)、LSN(点数)、FSA(起始角x100)、LSA(结束角x100)
*******************************************************************************/
#include "app_radar.h"
#include "app_config.h"
#include "bsp_time.h"
#include "bsp_uart.h"

static uint32_t s_bytes = 0;        /* 累计收到字节 */
static uint32_t s_hdr = 0;          /* 累计包头个数 */
static uint32_t s_lastBytes = 0;
static uint32_t s_lastHdr = 0;
static uint32_t s_tStat = 0;

static uint8_t  s_prev = 0;         /* 上一字节（用来找包头） */
static uint8_t  s_hdrCnt = 0;       /* 包头之后已抓到的字段数 */
static uint8_t  s_ct = 0;
static uint8_t  s_lsn = 0;
static uint16_t s_fsa = 0;
static uint16_t s_lsa = 0;
static uint8_t  s_hasPkt = 0;

static void radar_feed(uint8_t b)
{
    s_bytes++;

    /* 包头：X2 数据包是 0x55AA（低字节在前 -> AA 55）；上电信息是 A5 5A，两种都认 */
    if((s_prev == 0xAAu && b == 0x55u) || (s_prev == 0xA5u && b == 0x5Au))
    {
        s_hdr++;
        s_hdrCnt = 0;
        s_prev = b;
        return;
    }
    s_prev = b;

    if(s_hdrCnt < 6u)
    {
        if(s_hdrCnt == 0u)
        {
            s_ct = b;
        }
        else if(s_hdrCnt == 1u)
        {
            s_lsn = b;
        }
        else if(s_hdrCnt == 2u)
        {
            s_fsa = b;
        }
        else if(s_hdrCnt == 3u)
        {
            s_fsa |= (uint16_t)((uint16_t)b << 8);
        }
        else if(s_hdrCnt == 4u)
        {
            s_lsa = b;
        }
        else
        {
            s_lsa |= (uint16_t)((uint16_t)b << 8);
            s_hasPkt = 1;
        }
        s_hdrCnt++;
    }
}

void Radar_Init(void)
{
    s_bytes = 0;
    s_hdr = 0;
    s_lastBytes = 0;
    s_lastHdr = 0;
    s_tStat = BSP_Millis();
    s_prev = 0;
    s_hdrCnt = 0;
    s_hasPkt = 0;
}

void Radar_Task(uint32_t nowMs)
{
    uint8_t  chunk[64];
    uint16_t n, i;

    while((n = RADAR_Read(chunk, (uint16_t)sizeof(chunk))) > 0u)
    {
        for(i = 0; i < n; i++)
        {
            radar_feed(chunk[i]);
        }
    }

    if((uint32_t)(nowMs - s_tStat) >= RADAR_STAT_PERIOD_MS)
    {
        uint32_t dt = (uint32_t)(nowMs - s_tStat);
        uint32_t db = (uint32_t)(s_bytes - s_lastBytes);
        uint32_t dh = (uint32_t)(s_hdr - s_lastHdr);
        uint32_t bps = (dt != 0u) ? (uint32_t)(((uint64_t)db * 1000u) / dt) : 0u;
        uint32_t hps = (dt != 0u) ? (uint32_t)(((uint64_t)dh * 1000u) / dt) : 0u;

        s_tStat = nowMs;
        s_lastBytes = s_bytes;
        s_lastHdr = s_hdr;

        if(s_hasPkt)
        {
            DBG_Printf("[rad] rx=%u B/s hdr=%u/s | CT=0x%02X 点数=%u 起始角=%u.%02u 结束角=%u.%02u | 累计=%u 溢出=%u\r\n",
                       (unsigned int)bps, (unsigned int)hps,
                       (unsigned int)s_ct, (unsigned int)s_lsn,
                       (unsigned int)(s_fsa / 100u), (unsigned int)(s_fsa % 100u),
                       (unsigned int)(s_lsa / 100u), (unsigned int)(s_lsa % 100u),
                       (unsigned int)s_bytes, (unsigned int)RADAR_RxOverflow());
        }
        else
        {
            DBG_Printf("[rad] rx=%u B/s hdr=%u/s | 还没抓到包头 | 累计=%u 溢出=%u\r\n",
                       (unsigned int)bps, (unsigned int)hps,
                       (unsigned int)s_bytes, (unsigned int)RADAR_RxOverflow());
        }
    }
}

uint32_t Radar_GetBytes(void)
{
    return s_bytes;
}

uint32_t Radar_GetPackets(void)
{
    return s_hdr;
}
