/********************************** (C) COPYRIGHT *******************************
* File Name          : app_ui.c
* Description        : OLED 界面状态机（无网络转圈 / 已连接 / 方向箭头）
*                      字模来自 App/app_glyphs.h（tools/gen_glyphs.py 生成）
*                      非阻塞：内容变化才重建帧缓冲，每次只刷 4 页
*******************************************************************************/
#include "app_ui.h"
#include "app_glyphs.h"
#include "app_chassis.h"
#include "app_config.h"
#include "app_remote.h"
#include "bsp_oled.h"

/* ---------------- 状态 ---------------- */
static Ui_Screen_t s_screen = UI_SCREEN_CONNECTING;
static Ui_Dir_t    s_dir = UI_DIR_STOP;
static uint8_t     s_present = 0;
static uint8_t     s_spinPhase = 0;
static uint8_t     s_flushPage = 0;
static uint8_t     s_needRender = 1;
static uint32_t    s_tConnected = 0;
static uint32_t    s_tSpin = 0;

/* 每 8 个方向单位向量 x100（0° 正右，顺时针） */
static const int8_t SPIN_DIR[8][2] = {
    { 100, 0 }, { 71, 71 }, { 0, 100 }, { -71, 71 },
    { -100, 0 }, { -71, -71 }, { 0, -100 }, { 71, -71 }
};

/* ---------------- 字模查表（16x16 汉字） ---------------- */
typedef struct
{
    uint16_t       code;
    const uint8_t *data;
} ui_cn_t;

static const ui_cn_t CN_TABLE[] = {
    { 0x65E0, cn_65E0 },    /* 无 */
    { 0x7F51, cn_7F51 },    /* 网 */
    { 0x7EDC, cn_7EDC },    /* 络 */
    { 0x5DF2, cn_5DF2 },    /* 已 */
    { 0x8FDE, cn_8FDE },    /* 连 */
    { 0x63A5, cn_63A5 },    /* 接 */
    { 0x505C, cn_505C },    /* 停 */
    { 0x524D, cn_524D },    /* 前 */
    { 0x540E, cn_540E },    /* 后 */
    { 0x5DE6, cn_5DE6 },    /* 左 */
    { 0x53F3, cn_53F3 },    /* 右 */
    { 0x0000, 0 }
};

static const uint8_t *cn_lookup(uint16_t code)
{
    uint8_t i;
    for(i = 0; CN_TABLE[i].code != 0; i++)
    {
        if(CN_TABLE[i].code == code)
        {
            return CN_TABLE[i].data;
        }
    }
    return 0;
}

/* 画 UTF-8 文本（只支持表里有的汉字；返回结束 x） */
static int16_t ui_text(int16_t x, int16_t y, const char *s)
{
    while(*s)
    {
        uint16_t code = 0;
        uint8_t  c = (uint8_t)(*s);

        if(c < 0x80)
        {
            s += 1;
            continue;                       /* 本界面不用 ASCII，跳过 */
        }
        else if((c & 0xE0) == 0xC0 && s[1])
        {
            code = (uint16_t)(((c & 0x1F) << 6) | (s[1] & 0x3F));
            s += 2;
        }
        else if((c & 0xF0) == 0xE0 && s[1] && s[2])
        {
            code = (uint16_t)(((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F));
            s += 3;
        }
        else
        {
            s += 1;
            continue;
        }

        {
            const uint8_t *g = cn_lookup(code);
            if(g)
            {
                OLED_BlitMono(x, y, 16, 16, g);
                x = (int16_t)(x + 16);
            }
        }
    }
    return x;
}

static void ui_text_center(int16_t y, const char *s)
{
    const char *p = s;
    int16_t w = 0;

    while(*p)
    {
        uint8_t c = (uint8_t)(*p);
        if(c < 0x80)
        {
            p += 1;
        }
        else if((c & 0xE0) == 0xC0)
        {
            w = (int16_t)(w + 16);
            p += 2;
        }
        else if((c & 0xF0) == 0xE0)
        {
            w = (int16_t)(w + 16);
            p += 3;
        }
        else
        {
            p += 1;
        }
    }
    ui_text((int16_t)((OLED_W - w) / 2), y, s);
}

/* ---------------- 方向判断（用底盘解算后的 x/y） ---------------- */
static Ui_Dir_t ui_dir_from_xy(int16_t x, int16_t y)
{
    int16_t ax = (x < 0) ? (int16_t)(-x) : x;
    int16_t ay = (y < 0) ? (int16_t)(-y) : y;

    if(ax == 0 && ay == 0)
    {
        return UI_DIR_STOP;
    }
    if(ay > (int16_t)(ax * 2))
    {
        return (y > 0) ? UI_DIR_FWD : UI_DIR_BACK;
    }
    if(ax > (int16_t)(ay * 2))
    {
        return (x > 0) ? UI_DIR_RIGHT : UI_DIR_LEFT;
    }
    if(y > 0)
    {
        return (x > 0) ? UI_DIR_FR : UI_DIR_FL;
    }
    return (x > 0) ? UI_DIR_BR : UI_DIR_BL;
}

static const uint8_t *ui_dir_icon(Ui_Dir_t d)
{
    switch(d)
    {
        case UI_DIR_FWD:   return arrow_up;
        case UI_DIR_BACK:  return arrow_down;
        case UI_DIR_LEFT:  return arrow_left;
        case UI_DIR_RIGHT: return arrow_right;
        case UI_DIR_FL:    return arrow_upleft;
        case UI_DIR_FR:    return arrow_upright;
        case UI_DIR_BL:    return arrow_downleft;
        case UI_DIR_BR:    return arrow_downright;
        default:           return icon_stop;
    }
}

static const char *ui_dir_label(Ui_Dir_t d)
{
    switch(d)
    {
        case UI_DIR_FWD:   return "前";
        case UI_DIR_BACK:  return "后";
        case UI_DIR_LEFT:  return "左";
        case UI_DIR_RIGHT: return "右";
        case UI_DIR_FL:    return "前左";
        case UI_DIR_FR:    return "前右";
        case UI_DIR_BL:    return "后左";
        case UI_DIR_BR:    return "后右";
        default:           return "停";
    }
}

/* ---------------- 三个画面 ---------------- */
static void ui_render_connecting(void)
{
    int16_t cx = OLED_W / 2, cy = 34;
    uint8_t i;

    OLED_Clear();
    ui_text_center(0, "无网络");

    /* 转圈：8 个点，当前点大、后两个渐小 */
    for(i = 0; i < 8; i++)
    {
        int16_t dx = (int16_t)(SPIN_DIR[i][0] * 13 / 100);
        int16_t dy = (int16_t)(SPIN_DIR[i][1] * 13 / 100);
        uint8_t head = (uint8_t)((i == s_spinPhase) || (i == ((s_spinPhase + 7) & 7)));
        uint8_t tail = (uint8_t)(i == ((s_spinPhase + 6) & 7));

        if(head)
        {
            OLED_FillRect((int16_t)(cx + dx - 2), (int16_t)(cy + dy - 2), 5, 5, 1);
        }
        else if(tail)
        {
            OLED_FillRect((int16_t)(cx + dx - 1), (int16_t)(cy + dy - 1), 3, 3, 1);
        }
        else
        {
            OLED_FillRect((int16_t)(cx + dx), (int16_t)(cy + dy), 2, 2, 1);
        }
    }
}

static void ui_render_connected(void)
{
    OLED_Clear();
    ui_text_center(8, "已连接");
    /* 对勾 */
    OLED_Line(46, 46, 57, 55, 1);
    OLED_Line(57, 55, 84, 28, 1);
    OLED_Line(47, 46, 58, 55, 1);
    OLED_Line(58, 55, 85, 28, 1);
}

static void ui_render_direction(Ui_Dir_t d)
{
    OLED_Clear();
    OLED_BlitMono(44, 0, 40, 40, ui_dir_icon(d));   /* 40x40 图标居中 */
    ui_text_center(48, ui_dir_label(d));
}

/* ---------------- 对外接口 ---------------- */
void App_Ui_Init(void)
{
    s_screen = UI_SCREEN_CONNECTING;
    s_dir = UI_DIR_STOP;
    s_spinPhase = 0;
    s_flushPage = 0;
    s_needRender = 1;
    s_tConnected = 0;
    s_tSpin = 0;

    s_present = (uint8_t)(OLED_Init() == 0);
}

Ui_Screen_t App_Ui_GetScreen(void)
{
    return s_screen;
}

Ui_Dir_t App_Ui_GetDirection(void)
{
    return s_dir;
}

const char *App_Ui_ScreenName(Ui_Screen_t s)
{
    switch(s)
    {
        case UI_SCREEN_CONNECTING: return "connecting";
        case UI_SCREEN_CONNECTED:  return "connected";
        case UI_SCREEN_DIRECTION:  return "direction";
        default:                   return "unknown";
    }
}

const char *App_Ui_DirName(Ui_Dir_t d)
{
    switch(d)
    {
        case UI_DIR_FWD:   return "forward";
        case UI_DIR_BACK:  return "back";
        case UI_DIR_LEFT:  return "left";
        case UI_DIR_RIGHT: return "right";
        case UI_DIR_FL:    return "fwd-left";
        case UI_DIR_FR:    return "fwd-right";
        case UI_DIR_BL:    return "back-left";
        case UI_DIR_BR:    return "back-right";
        default:           return "stop";
    }
}

void App_Ui_Update(uint32_t nowMs)
{
    const Remote_State_t   *rm;
    const Chassis_Output_t *ch;
    uint8_t                 net;

    if(!s_present)
    {
        return;
    }

    rm = Remote_Get();
    ch = Chassis_Get();
    net = rm->net;

    /* ---- 状态迁移 ---- */
    switch(s_screen)
    {
        case UI_SCREEN_CONNECTING:
            if(net)
            {
                s_screen = UI_SCREEN_CONNECTED;
                s_tConnected = nowMs;
                s_needRender = 1;
            }
            break;

        case UI_SCREEN_CONNECTED:
            if(!net)
            {
                s_screen = UI_SCREEN_CONNECTING;
                s_needRender = 1;
            }
            else if((uint32_t)(nowMs - s_tConnected) >= UI_CONNECTED_HOLD_MS)
            {
                s_screen = UI_SCREEN_DIRECTION;
                s_needRender = 1;
            }
            break;

        case UI_SCREEN_DIRECTION:
        default:
            if(!net)
            {
                s_screen = UI_SCREEN_CONNECTING;
                s_needRender = 1;
            }
            else
            {
                Ui_Dir_t d = ui_dir_from_xy(ch->x, ch->y);
                if(d != s_dir)
                {
                    s_dir = d;
                    s_needRender = 1;
                }
            }
            break;
    }

    /* ---- 转圈动画 ---- */
    if(s_screen == UI_SCREEN_CONNECTING &&
       (uint32_t)(nowMs - s_tSpin) >= UI_SPINNER_STEP_MS)
    {
        s_tSpin = nowMs;
        s_spinPhase = (uint8_t)((s_spinPhase + 1) & 7);
        s_needRender = 1;
    }

    /* ---- 内容变化才重建帧缓冲 ---- */
    if(s_needRender)
    {
        s_needRender = 0;
        switch(s_screen)
        {
            case UI_SCREEN_CONNECTED: ui_render_connected(); break;
            case UI_SCREEN_DIRECTION: ui_render_direction(s_dir); break;
            default:                  ui_render_connecting(); break;
        }
    }

    /* ---- 分页刷屏：每次 4 页（8 页两趟刷完一帧，约 12ms/次）---- */
    (void)OLED_FlushPart(s_flushPage, 4);
    s_flushPage = (uint8_t)((s_flushPage + 4) & 7);
}
