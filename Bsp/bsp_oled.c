/********************************** (C) COPYRIGHT *******************************
* File Name          : bsp_oled.c
* Description        : 0.96" SSD1306 OLED（软 I2C，PB6=SCL / PB7=SDA）
*                      移植自本项目早期 OLED_Dxy 工程（那版已实测可用）
*                      帧缓冲 128x64 = 1024 字节，支持分页刷新（不阻塞主循环）
*******************************************************************************/
#include "bsp_oled.h"
#include "ch32v30x.h"

/* ---------------- 软 I2C 底层（GPIOB 寄存器直接操作） ---------------- */
#define PB_CFGLR    (*(volatile uint32_t *)0x40010C00)
#define PB_INDR     (*(volatile uint32_t *)0x40010C08)
#define PB_BSHR     (*(volatile uint32_t *)0x40010C10)
#define PB_BCR      (*(volatile uint32_t *)0x40010C14)

#define SCL_SHIFT   24      /* PB6 = CFGLR bit24-27 */
#define SDA_SHIFT   28      /* PB7 = CFGLR bit28-31 */

/* 释放总线（高）：输入 + 内部上拉 —— I2C 靠外部/内部上拉拉高 */
static inline void pin_release(uint8_t shift)
{
    uint32_t t = PB_CFGLR;
    t &= ~((uint32_t)0x0F << shift);
    t |= ((uint32_t)0x08 << shift);          /* CNF=10, MODE=00 -> 输入带上拉 */
    PB_CFGLR = t;
    PB_BSHR = (uint32_t)1 << (shift / 4);    /* ODR=1 -> 上拉 */
}

/* 拉低：推挽输出低 */
static inline void pin_low(uint8_t shift)
{
    uint32_t t = PB_CFGLR;
    t &= ~((uint32_t)0x0F << shift);
    t |= ((uint32_t)0x03 << shift);          /* CNF=00, MODE=11 -> 推挽 50MHz */
    PB_CFGLR = t;
    PB_BCR = (uint32_t)1 << (shift / 4);
}

#define SCL_H()     pin_release(SCL_SHIFT)
#define SCL_L()     pin_low(SCL_SHIFT)
#define SDA_H()     pin_release(SDA_SHIFT)
#define SDA_L()     pin_low(SDA_SHIFT)
#define SDA_READ()  ((PB_INDR >> 7) & 1u)
#define SCL_READ()  ((PB_INDR >> 6) & 1u)

static void i2c_delay(void)
{
    volatile uint32_t i;
    for(i = 0; i < 20; i++)     /* 约 1us 半周期 -> 总线 ~400kHz */
    {
        __asm volatile("nop");
    }
}

static void I2C_Start(void)
{
    SDA_H(); SCL_H(); i2c_delay();
    SDA_L(); i2c_delay();
    SCL_L(); i2c_delay();
}

static void I2C_Stop(void)
{
    SDA_L(); i2c_delay();
    SCL_H(); i2c_delay();
    SDA_H(); i2c_delay();
}

/* 0 = 收到 ACK, 1 = 无应答 */
static uint8_t I2C_WriteByte(uint8_t data)
{
    uint8_t i, ack;

    for(i = 0; i < 8; i++)
    {
        if(data & 0x80)
        {
            SDA_H();
        }
        else
        {
            SDA_L();
        }
        i2c_delay();
        SCL_H(); i2c_delay();
        SCL_L(); i2c_delay();
        data <<= 1;
    }

    SDA_H();                    /* 释放 SDA 读 ACK */
    i2c_delay();
    SCL_H(); i2c_delay();
    ack = SDA_READ() ? 1 : 0;
    SCL_L(); i2c_delay();
    return ack;
}

/* ctrl: 0x00 = 命令, 0x40 = 数据 */
static uint8_t OLED_Write(uint8_t addr, uint8_t ctrl, const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint8_t err = 0;

    I2C_Start();
    if(I2C_WriteByte((uint8_t)(addr << 1))) err = 1;
    if(!err && I2C_WriteByte(ctrl))         err = 1;
    for(i = 0; i < len && !err; i++)
    {
        if(I2C_WriteByte(data[i]))          err = 1;
    }
    I2C_Stop();
    return err;
}

static uint8_t OLED_Cmd(uint8_t addr, uint8_t cmd)
{
    return OLED_Write(addr, 0x00, &cmd, 1);
}

/* ---------------- 帧缓冲 ---------------- */
static uint8_t  s_gram[OLED_W * OLED_H / 8];
static uint8_t  s_addr = 0x3C;
static uint8_t  s_ready = 0;
static uint8_t  s_flushPage = 0;

uint8_t OLED_IsReady(void)
{
    return s_ready;
}

void OLED_Clear(void)
{
    uint16_t i;
    for(i = 0; i < sizeof(s_gram); i++)
    {
        s_gram[i] = 0x00;
    }
}

void OLED_SetPixel(int16_t x, int16_t y, uint8_t on)
{
    uint16_t idx;

    if(x < 0 || x >= OLED_W || y < 0 || y >= OLED_H)
    {
        return;
    }
    idx = (uint16_t)((y >> 3) * OLED_W + x);
    if(on)
    {
        s_gram[idx] |= (uint8_t)(1u << (y & 7));
    }
    else
    {
        s_gram[idx] &= (uint8_t)~(1u << (y & 7));
    }
}

void OLED_HLine(int16_t x, int16_t y, int16_t w, uint8_t on)
{
    int16_t i;
    for(i = 0; i < w; i++)
    {
        OLED_SetPixel((int16_t)(x + i), y, on);
    }
}

void OLED_VLine(int16_t x, int16_t y, int16_t h, uint8_t on)
{
    int16_t i;
    for(i = 0; i < h; i++)
    {
        OLED_SetPixel(x, (int16_t)(y + i), on);
    }
}

void OLED_Line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t on)
{
    int16_t dx = (x1 > x0) ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
    int16_t dy = (y1 > y0) ? (int16_t)(y1 - y0) : (int16_t)(y0 - y1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (int16_t)(dx - dy);

    for(;;)
    {
        int16_t e2;

        OLED_SetPixel(x0, y0, on);
        if(x0 == x1 && y0 == y1)
        {
            break;
        }
        e2 = (int16_t)(err * 2);
        if(e2 > -dy)
        {
            err -= dy;
            x0 += sx;
        }
        if(e2 < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void OLED_Rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t on)
{
    OLED_HLine(x, y, w, on);
    OLED_HLine(x, (int16_t)(y + h - 1), w, on);
    OLED_VLine(x, y, h, on);
    OLED_VLine((int16_t)(x + w - 1), y, h, on);
}

void OLED_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t on)
{
    int16_t i;
    for(i = 0; i < h; i++)
    {
        OLED_HLine(x, (int16_t)(y + i), w, on);
    }
}

/*********************************************************************
 * @fn      OLED_DrawGlyph16
 * @brief   画 16 高的字模（列优先按页，bit0 在上）
 * @param   w - 字宽 8（ASCII）或 16（中文）；y 必须 8 对齐
 *********************************************************************/
void OLED_DrawGlyph16(int16_t x, int16_t y, const uint8_t *data, uint8_t w)
{
    uint8_t page, col, bit;

    for(page = 0; page < 2; page++)
    {
        for(col = 0; col < w; col++)
        {
            uint8_t b = data[page * w + col];
            if(b == 0)
            {
                continue;
            }
            for(bit = 0; bit < 8; bit++)
            {
                if(b & (1u << bit))
                {
                    OLED_SetPixel((int16_t)(x + col), (int16_t)(y + page * 8 + bit), 1);
                }
            }
        }
    }
}

/* ---------------- SSD1306 ---------------- */
static uint8_t OLED_InitSeq(void)
{
    static const uint8_t init_cmds[] = {
        0xAE,               /* display off */
        0xD5, 0x80,         /* clock divide */
        0xA8, 0x3F,         /* multiplex 1/64 */
        0xD3, 0x00,         /* display offset */
        0x40,               /* start line 0 */
        0x8D, 0x14,         /* charge pump on */
        0x20, 0x00,         /* horizontal addressing */
        0xA1,               /* segment remap */
        0xC8,               /* COM scan direction */
        0xDA, 0x12,         /* COM pins */
        0x81, 0xCF,         /* contrast */
        0xD9, 0xF1,         /* pre-charge */
        0xDB, 0x40,         /* VCOMH */
        0xA4,               /* resume from RAM */
        0xA6,               /* normal (not inverted) */
        0x2E,               /* deactivate scroll */
        0xAF                /* display on */
    };

    return OLED_Write(s_addr, 0x00, init_cmds, (uint16_t)sizeof(init_cmds));
}

/* 总线自检：两条线释放后应为高（没屏/没上拉/接错会读到 0） */
static uint8_t I2C_BusIdleOk(void)
{
    SDA_H(); SCL_H();
    i2c_delay(); i2c_delay();
    return (uint8_t)(SCL_READ() && SDA_READ());
}

uint8_t OLED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    uint8_t addr;
    uint8_t found = 0;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    SCL_H();
    SDA_H();

    s_ready = 0;
    OLED_Clear();
    s_flushPage = 0;

    if(!I2C_BusIdleOk())
    {
        return 1;                       /* 总线都没拉高，多半没接屏 */
    }

    /* 先试 0x3C（最常见），不行再扫一遍总线 */
    if(OLED_Cmd(0x3C, 0xAE) == 0)
    {
        s_addr = 0x3C;
        found = 1;
    }
    else
    {
        for(addr = 0x08; addr < 0x78; addr++)
        {
            if(OLED_Cmd(addr, 0xAE) == 0)
            {
                s_addr = addr;
                found = 1;
                break;
            }
        }
    }

    if(!found)
    {
        return 1;
    }

    s_ready = (uint8_t)(OLED_InitSeq() == 0);
    if(s_ready)
    {
        OLED_Flush();
    }
    return (uint8_t)(s_ready ? 0 : 1);
}

/*********************************************************************
 * @fn      OLED_Flush
 * @brief   整屏刷新（8 页一次写完，阻塞 ~90ms，只在初始化时用）
 *********************************************************************/
uint8_t OLED_Flush(void)
{
    uint8_t page;
    uint8_t cmds[3];
    uint8_t err = 0;

    if(!s_ready)
    {
        return 1;
    }

    for(page = 0; page < OLED_H / 8; page++)
    {
        cmds[0] = (uint8_t)(0xB0 | page);
        cmds[1] = 0x00;
        cmds[2] = 0x10;
        err |= OLED_Write(s_addr, 0x00, cmds, 3);
        err |= OLED_Write(s_addr, 0x40, &s_gram[page * OLED_W], OLED_W);
    }
    s_flushPage = 0;
    return err;
}

/*********************************************************************
 * @fn      OLED_BlitMono
 * @brief   贴单色位图（行优先、MSB 在左），app_glyphs.h 的字模就是这个格式
 *********************************************************************/
void OLED_BlitMono(int16_t x, int16_t y, uint8_t w, uint8_t h, const uint8_t *data)
{
    uint8_t bytesPerRow = (uint8_t)((w + 7) / 8);
    uint8_t row, col;

    for(row = 0; row < h; row++)
    {
        for(col = 0; col < w; col++)
        {
            uint8_t b = data[(uint16_t)row * bytesPerRow + (col >> 3)];
            if(b & (uint8_t)(0x80u >> (col & 7)))
            {
                OLED_SetPixel((int16_t)(x + col), (int16_t)(y + row), 1);
            }
        }
    }
}

/*********************************************************************
 * @fn      OLED_FlushPart
 * @brief   只刷 pageCount 个页（从 firstPage 开始），一页约 3ms
 *          返回 0 = 正常，1 = 没屏或写失败
 *********************************************************************/
uint8_t OLED_FlushPart(uint8_t firstPage, uint8_t pageCount)
{
    uint8_t cmds[3];
    uint8_t err = 0;
    uint8_t i;

    if(!s_ready)
    {
        return 1;
    }
    if((uint16_t)firstPage + pageCount > OLED_H / 8)
    {
        pageCount = (uint8_t)(OLED_H / 8 - firstPage);
    }

    for(i = 0; i < pageCount; i++)
    {
        uint8_t page = (uint8_t)(firstPage + i);
        cmds[0] = (uint8_t)(0xB0 | page);
        cmds[1] = 0x00;
        cmds[2] = 0x10;
        err |= OLED_Write(s_addr, 0x00, cmds, 3);
        err |= OLED_Write(s_addr, 0x40, &s_gram[page * OLED_W], OLED_W);
    }
    return err;
}
