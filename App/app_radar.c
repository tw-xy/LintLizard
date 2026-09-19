/********************************** (C) COPYRIGHT *******************************
* File Name          : app_radar.c
* Description        : YDLIDAR X2 兼容雷达解析
*                      AA 55 | CT | LSN | FSA(2) | LSA(2) | S1(2)..Sn(2) | CS(2)
*                      校验和：前 8 字节与每个采样点按 16 位小端逐字异或
*                      距离：采样字 >> 2，单位 mm
*******************************************************************************/
#include "app_radar.h"
#include "app_config.h"
#include "bsp_time.h"
#include "bsp_uart.h"

#define RADAR_MAX_POINTS      80u
#define RADAR_MAX_PKT_BYTES  (10u + 2u * RADAR_MAX_POINTS)
#define RADAR_NO_DIST        0xFFFFu
#define RADAR_HIST_LEN        48u

/* 累计统计 */
static uint32_t s_bytes = 0;
static uint32_t s_hdr = 0;
static uint32_t s_pktOk = 0;
static uint32_t s_csErr = 0;
static uint32_t s_fmtErr = 0;

/* 上一次统计快照 */
static uint32_t s_lastBytes = 0;
static uint32_t s_lastHdr = 0;
static uint32_t s_lastOk = 0;
static uint32_t s_lastCsErr = 0;
static uint32_t s_lastFmtErr = 0;
static uint32_t s_tStat = 0;

/* 接收状态机 */
static uint8_t  s_prev = 0;
static uint8_t  s_pkt[RADAR_MAX_PKT_BYTES];
static uint16_t s_pos = 0;
static uint16_t s_need = 0;
static uint8_t  s_lastLsn = 0;

/* 最近有效包与滑动窗口历史（48 包，约 280ms） */
static uint8_t  s_havePkt = 0;
static uint32_t s_lastPktMs = 0;
static uint16_t s_frontHist[RADAR_HIST_LEN];
static uint16_t s_leftHist[RADAR_HIST_LEN];
static uint16_t s_rightHist[RADAR_HIST_LEN];
static uint8_t  s_histPos = 0;

/* X2 三角测距角度二级修正，单位 0.1°，按 100mm 建表。
 * 值来自官方公式：atan(21.8*(155.3-D)/(155.3*D))*180/PI
 * D=0 时无有效测距，表中置 0。 */
static const int16_t s_angCorr10[81] = {
       0,    44,   -18,   -39,   -49,   -55,   -59,   -62,   -65,   -66,   -68,   -69,
     -70,   -70,   -71,   -72,   -72,   -73,   -73,   -73,   -74,   -74,   -74,   -75,
     -75,   -75,   -75,   -75,   -76,   -76,   -76,   -76,   -76,   -76,   -76,   -76,
     -77,   -77,   -77,   -77,   -77,   -77,   -77,   -77,   -77,   -77,   -77,   -77,
     -77,   -77,   -77,   -78,   -78,   -78,   -78,   -78,   -78,   -78,   -78,   -78,
     -78,   -78,   -78,   -78,   -78,   -78,   -78,   -78,   -78,   -78,   -78,   -78,
     -78,   -78,   -78,   -78,   -78,   -78,   -78,   -78,   -78
};

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int16_t ang_corr_10(uint16_t distMm)
{
    uint16_t idx;
    uint16_t rem;
    int32_t  a;
    int32_t  b;

    if(distMm >= AVOID_MAX_VALID_MM)
    {
        return s_angCorr10[80];
    }

    idx = (uint16_t)(distMm / 100u);
    rem = (uint16_t)(distMm % 100u);
    a = s_angCorr10[idx];
    b = s_angCorr10[idx + 1u];
    return (int16_t)(a + ((b - a) * (int32_t)rem) / 100);
}

static int32_t angle_diff_cdeg(int32_t a, int32_t b)
{
    int32_t d = a - b;

    while(d < -18000)
    {
        d += 36000;
    }
    while(d > 18000)
    {
        d -= 36000;
    }
    return d;
}

static uint8_t in_sector(int32_t angleCdeg, int32_t centerCdeg, int32_t halfCdeg)
{
    int32_t d = angle_diff_cdeg(angleCdeg, centerCdeg);

    return (uint8_t)((d >= -halfCdeg && d <= halfCdeg) ? 1u : 0u);
}

static void radar_hist_push(uint16_t *hist, uint16_t distMm)
{
    hist[s_histPos] = distMm;
}

static uint16_t radar_hist_min(const uint16_t *hist)
{
    uint16_t i;
    uint16_t min = RADAR_NO_DIST;

    for(i = 0u; i < RADAR_HIST_LEN; i++)
    {
        if(hist[i] < min)
        {
            min = hist[i];
        }
    }
    return min;
}

static void radar_reset_pkt(void)
{
    s_pos = 0;
    s_need = 0;
}

/*********************************************************************
 * @fn      radar_parse_packet
 * @brief   校验并解析一个完整 X2 包，更新前/左/右扇区最小距离
 *********************************************************************/
static void radar_parse_packet(const uint8_t *pkt, uint32_t nowMs)
{
    uint8_t  lsn = pkt[3];
    uint16_t fsa = rd_u16(&pkt[4]);
    uint16_t lsa = rd_u16(&pkt[6]);
    uint16_t cs = 0;
    uint16_t csPkt;
    uint16_t i;
    int32_t  saCdeg;
    int32_t  eaCdeg;
    int32_t  diffCdeg;
    int32_t  stepCdeg;
    int32_t  frontCenter = AVOID_FRONT_OFFSET_CDEG;
    int32_t  leftCenter = frontCenter - 9000;
    int32_t  rightCenter = frontCenter + 9000;
    uint16_t pktFront = RADAR_NO_DIST;
    uint16_t pktLeft = RADAR_NO_DIST;
    uint16_t pktRight = RADAR_NO_DIST;
    uint8_t  cntFront = 0;
    uint8_t  cntLeft = 0;
    uint8_t  cntRight = 0;

    if(lsn == 0u || lsn > RADAR_MAX_POINTS)
    {
        s_fmtErr++;
        return;
    }

    /* CS = 前 8 字节 + 每个 2 字节采样，按 16 位小端逐字异或 */
    for(i = 0; i < 8u; i += 2u)
    {
        cs ^= rd_u16(&pkt[i]);
    }
    for(i = 0; i < lsn; i++)
    {
        cs ^= rd_u16(&pkt[8u + 2u * i]);
    }
    csPkt = rd_u16(&pkt[8u + 2u * lsn]);
    if(cs != csPkt)
    {
        s_csErr++;
        return;
    }

    /* FSA/LSA 的最低位是固定校验位 1 */
    if(((fsa & 1u) == 0u) || ((lsa & 1u) == 0u))
    {
        s_fmtErr++;
        return;
    }

    saCdeg = (int32_t)(((uint32_t)(fsa >> 1) * 25u) / 16u);
    eaCdeg = (int32_t)(((uint32_t)(lsa >> 1) * 25u) / 16u);
    diffCdeg = eaCdeg - saCdeg;
    if(diffCdeg < 0)
    {
        diffCdeg += 36000;
    }
    stepCdeg = (lsn > 1u) ? (diffCdeg / (int32_t)(lsn - 1u)) : 0;

    for(i = 0; i < lsn; i++)
    {
        uint16_t raw = rd_u16(&pkt[8u + 2u * i]);
        uint16_t distMm;
        int32_t  angleCdeg;

        if(raw == 0u)
        {
            continue;                       /* 无回波 */
        }

        distMm = (uint16_t)(raw >> 2);      /* 低 2 位是干扰标记 */
        if(distMm == 0u || distMm > AVOID_MAX_VALID_MM)
        {
            continue;
        }
        if(distMm < AVOID_MIN_VALID_MM)
        {
            distMm = AVOID_MIN_VALID_MM;    /* 近距回波按最小测距处理，偏安全 */
        }

        angleCdeg = saCdeg + stepCdeg * (int32_t)i;
        angleCdeg += (int32_t)ang_corr_10(distMm) * 10;  /* 0.1° -> 0.01° */
        while(angleCdeg < 0)
        {
            angleCdeg += 36000;
        }
        while(angleCdeg >= 36000)
        {
            angleCdeg -= 36000;
        }

        if(in_sector(angleCdeg, frontCenter, AVOID_FRONT_HALF_ANGLE_CDEG))
        {
            cntFront++;
            if(distMm < pktFront)
            {
                pktFront = distMm;
            }
        }
        if(in_sector(angleCdeg, leftCenter, AVOID_SIDE_HALF_ANGLE_CDEG))
        {
            cntLeft++;
            if(distMm < pktLeft)
            {
                pktLeft = distMm;
            }
        }
        if(in_sector(angleCdeg, rightCenter, AVOID_SIDE_HALF_ANGLE_CDEG))
        {
            cntRight++;
            if(distMm < pktRight)
            {
                pktRight = distMm;
            }
        }
    }

    radar_hist_push(s_frontHist, (cntFront >= AVOID_MIN_POINTS) ? pktFront : RADAR_NO_DIST);
    radar_hist_push(s_leftHist,  (cntLeft  >= AVOID_MIN_POINTS) ? pktLeft  : RADAR_NO_DIST);
    radar_hist_push(s_rightHist, (cntRight >= AVOID_MIN_POINTS) ? pktRight : RADAR_NO_DIST);
    s_histPos = (uint8_t)((s_histPos + 1u) % RADAR_HIST_LEN);

    s_lastLsn = lsn;
    s_pktOk++;
    s_havePkt = 1;
    s_lastPktMs = nowMs;
}

/*********************************************************************
 * @fn      radar_feed
 * @brief   逐字节组包：只在 s_pos==0 时寻找 AA 55，包内不会再误同步
 *********************************************************************/
static void radar_feed(uint8_t b, uint32_t nowMs)
{
    s_bytes++;

    if(s_pos == 0u)
    {
        if(s_prev == 0xAAu && b == 0x55u)
        {
            s_pkt[0] = 0xAAu;
            s_pkt[1] = 0x55u;
            s_pos = 2u;
            s_hdr++;
        }
        s_prev = b;
        return;
    }

    if(s_pos >= (uint16_t)sizeof(s_pkt))
    {
        s_fmtErr++;
        radar_reset_pkt();
        s_prev = b;
        return;
    }

    s_pkt[s_pos++] = b;

    if(s_pos == 8u)
    {
        uint8_t lsn = s_pkt[3];

        if(lsn == 0u || lsn > RADAR_MAX_POINTS)
        {
            s_fmtErr++;
            radar_reset_pkt();
            s_prev = b;
            return;
        }
        s_need = (uint16_t)(10u + 2u * lsn);
    }

    if(s_pos >= 10u && s_pos == s_need)
    {
        radar_parse_packet(s_pkt, nowMs);
        radar_reset_pkt();
    }

    s_prev = b;
}

void Radar_Init(void)
{
    s_bytes = 0;
    s_hdr = 0;
    s_pktOk = 0;
    s_csErr = 0;
    s_fmtErr = 0;
    s_lastBytes = 0;
    s_lastHdr = 0;
    s_lastOk = 0;
    s_lastCsErr = 0;
    s_lastFmtErr = 0;
    s_tStat = BSP_Millis();
    s_prev = 0;
    s_pos = 0;
    s_need = 0;
    s_lastLsn = 0;
    s_havePkt = 0;
    s_lastPktMs = 0;
    for(s_histPos = 0u; s_histPos < RADAR_HIST_LEN; s_histPos++)
    {
        s_frontHist[s_histPos] = RADAR_NO_DIST;
        s_leftHist[s_histPos] = RADAR_NO_DIST;
        s_rightHist[s_histPos] = RADAR_NO_DIST;
    }
    s_histPos = 0;
}

void Radar_Task(uint32_t nowMs)
{
    uint8_t  chunk[64];
    uint16_t n;
    uint16_t i;

    while((n = RADAR_Read(chunk, (uint16_t)sizeof(chunk))) > 0u)
    {
        for(i = 0; i < n; i++)
        {
            radar_feed(chunk[i], nowMs);
        }
    }

    if((uint32_t)(nowMs - s_tStat) >= RADAR_STAT_PERIOD_MS)
    {
        uint32_t dt = (uint32_t)(nowMs - s_tStat);
        uint32_t db = (uint32_t)(s_bytes - s_lastBytes);
        uint32_t dh = (uint32_t)(s_hdr - s_lastHdr);
        uint32_t dok = (uint32_t)(s_pktOk - s_lastOk);
        uint32_t dcs = (uint32_t)(s_csErr - s_lastCsErr);
        uint32_t dfmt = (uint32_t)(s_fmtErr - s_lastFmtErr);
        uint32_t bps = (dt != 0u) ? (uint32_t)(((uint64_t)db * 1000u) / dt) : 0u;
        uint32_t hps = (dt != 0u) ? (uint32_t)(((uint64_t)dh * 1000u) / dt) : 0u;
        uint32_t okps = (dt != 0u) ? (uint32_t)(((uint64_t)dok * 1000u) / dt) : 0u;
        uint16_t front = Radar_GetFrontMm();
        uint16_t left = Radar_GetLeftMm();
        uint16_t right = Radar_GetRightMm();
        uint16_t frontShow = (front == RADAR_NO_DIST) ? 0u : front;
        uint16_t leftShow = (left == RADAR_NO_DIST) ? 0u : left;
        uint16_t rightShow = (right == RADAR_NO_DIST) ? 0u : right;

        s_tStat = nowMs;
        s_lastBytes = s_bytes;
        s_lastHdr = s_hdr;
        s_lastOk = s_pktOk;
        s_lastCsErr = s_csErr;
        s_lastFmtErr = s_fmtErr;

        DBG_Printf("[rad] rx=%u B/s hdr=%u/s ok=%u/s | pts=%u front=%u left=%u right=%u mm | csErr=%u fmtErr=%u ovf=%u\r\n",
                   (unsigned int)bps, (unsigned int)hps, (unsigned int)okps,
                   (unsigned int)s_lastLsn,
                   (unsigned int)frontShow, (unsigned int)leftShow, (unsigned int)rightShow,
                   (unsigned int)dcs, (unsigned int)dfmt,
                   (unsigned int)RADAR_RxOverflow());
    }
}

uint32_t Radar_GetBytes(void)
{
    return s_bytes;
}

uint32_t Radar_GetPackets(void)
{
    return s_pktOk;
}

uint32_t Radar_GetChecksumErrors(void)
{
    return s_csErr;
}

uint16_t Radar_GetFrontMm(void)
{
    return radar_hist_min(s_frontHist);
}

uint16_t Radar_GetLeftMm(void)
{
    return radar_hist_min(s_leftHist);
}

uint16_t Radar_GetRightMm(void)
{
    return radar_hist_min(s_rightHist);
}

uint8_t Radar_IsOnline(uint32_t nowMs)
{
    if(!s_havePkt)
    {
        return 0;
    }
    return (uint8_t)(((uint32_t)(nowMs - s_lastPktMs) <= AVOID_TIMEOUT_MS) ? 1u : 0u);
}
