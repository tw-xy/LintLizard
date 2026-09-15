/********************************** (C) COPYRIGHT *******************************
* File Name          : bsp_uart.c
* Description        : 串口 BSP
*                      USART1(PA9/PA10) -> 板载 WCH-Link -> COM4，非阻塞打印
*                      USART3(PB10/PB11) -> ESP8266-01S，中断收发 + 环形缓冲
*                      主循环只搬数据，ISR 只进出缓冲，全程无死等
*******************************************************************************/
#include "bsp_uart.h"
#include "app_config.h"
#include "ch32v30x.h"

#include <stdarg.h>
#include <stdio.h>

/* ============================ 环形缓冲 ============================ */
#define DBG_TX_SIZE   512u   /* 必须是 2 的幂 */
#define ESP_RX_SIZE   256u
#define ESP_TX_SIZE   256u

typedef struct
{
    uint8_t           *buf;
    uint16_t           mask;
    volatile uint16_t  head;      /* 生产者写入位置 */
    volatile uint16_t  tail;      /* 消费者读出位置 */
    volatile uint32_t  overflow;  /* 溢出丢弃计数 */
} ring_t;

static uint8_t s_dbgTxBuf[DBG_TX_SIZE];
static uint8_t s_espRxBuf[ESP_RX_SIZE];
static uint8_t s_espTxBuf[ESP_TX_SIZE];

static ring_t s_dbgTx = {s_dbgTxBuf, DBG_TX_SIZE - 1u, 0, 0, 0};
static ring_t s_espRx = {s_espRxBuf, ESP_RX_SIZE - 1u, 0, 0, 0};
static ring_t s_espTx = {s_espTxBuf, ESP_TX_SIZE - 1u, 0, 0, 0};

static volatile uint32_t s_espRxCount = 0;

static uint16_t ring_count(const ring_t *r)
{
    return (uint16_t)(r->head - r->tail);
}

static uint8_t ring_put(ring_t *r, uint8_t b)
{
    if(ring_count(r) > r->mask)          /* 满：丢新数据，绝不阻塞 */
    {
        r->overflow++;
        return 0;
    }
    r->buf[r->head & r->mask] = b;
    r->head++;
    return 1;
}

static uint8_t ring_get(ring_t *r, uint8_t *b)
{
    if(r->head == r->tail)
    {
        return 0;
    }
    *b = r->buf[r->tail & r->mask];
    r->tail++;
    return 1;
}

static void ring_put_buf(ring_t *r, const uint8_t *src, uint16_t len)
{
    while(len--)
    {
        ring_put(r, *src++);
    }
}

/* ============================ 中断服务 ============================ */
void USART1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/*********************************************************************
 * @fn      USART1_IRQHandler
 * @brief   调试口发送中断：把环形缓冲里的字节慢慢吐出去
 *********************************************************************/
void USART1_IRQHandler(void)
{
    if(USART_GetITStatus(USART1, USART_IT_TXE) != RESET)
    {
        uint8_t b;

        if(ring_get(&s_dbgTx, &b))
        {
            USART_SendData(USART1, b);
        }
        else
        {
            USART_ITConfig(USART1, USART_IT_TXE, DISABLE);
        }
    }
}

/*********************************************************************
 * @fn      USART3_IRQHandler
 * @brief   ESP8266 口：收字节进缓冲 / 发字节出缓冲
 *********************************************************************/
void USART3_IRQHandler(void)
{
    if(USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
    {
        ring_put(&s_espRx, (uint8_t)USART_ReceiveData(USART3));
        s_espRxCount++;
    }

    /* 溢出/帧错误（ESP 上电时 74880 波特的启动乱码会触发）：
       读一次 DR 清标志，脏数据直接丢掉，不影响后面的正常帧 */
    if(USART_GetFlagStatus(USART3, USART_FLAG_ORE) != RESET)
    {
        (void)USART_ReceiveData(USART3);
    }

    if(USART_GetITStatus(USART3, USART_IT_TXE) != RESET)
    {
        uint8_t b;

        if(ring_get(&s_espTx, &b))
        {
            USART_SendData(USART3, b);
        }
        else
        {
            USART_ITConfig(USART3, USART_IT_TXE, DISABLE);
        }
    }
}

/* ============================ 初始化 ============================ */
static void dbg_uart_init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;              /* USART1_TX */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;              /* USART1_RX，留给以后的调试命令 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = DBG_UART_BAUD;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStructure);

    USART_Cmd(USART1, ENABLE);
}

static void esp_uart_init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef  NVIC_InitStructure = {0};

    /* USART3 默认映射：TX = PB10，RX = PB11，不需要重映射 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_10;             /* USART3_TX -> ESP RX */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_11;              /* USART3_RX <- ESP TX */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = ESP_UART_BAUD;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART3, &USART_InitStructure);

    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART3, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;   /* 打印优先级最低 */
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

void BSP_Uart_Init(void)
{
    dbg_uart_init();
    esp_uart_init();
}

/* ============================ 调试口 API ============================ */
void DBG_Write(const uint8_t *data, uint16_t len)
{
    ring_put_buf(&s_dbgTx, data, len);
    USART_ITConfig(USART1, USART_IT_TXE, ENABLE);   /* 让 ISR 去取 */
}

void DBG_Printf(const char *fmt, ...)
{
    char    tmp[128];
    int     n;
    va_list ap;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if(n <= 0)
    {
        return;
    }
    if(n > (int)sizeof(tmp) - 1)
    {
        n = (int)sizeof(tmp) - 1;
    }
    DBG_Write((const uint8_t *)tmp, (uint16_t)n);
}

uint32_t DBG_TxDropped(void)
{
    return s_dbgTx.overflow;
}

/* ============================ ESP8266 口 API ============================ */
uint16_t ESP_Read(uint8_t *dst, uint16_t maxLen)
{
    uint16_t n = 0;

    while(n < maxLen)
    {
        if(!ring_get(&s_espRx, &dst[n]))
        {
            break;
        }
        n++;
    }
    return n;
}

uint16_t ESP_Write(const uint8_t *src, uint16_t len)
{
    uint16_t before = ring_count(&s_espTx);

    ring_put_buf(&s_espTx, src, len);
    USART_ITConfig(USART3, USART_IT_TXE, ENABLE);
    return (uint16_t)(ring_count(&s_espTx) - before);
}

uint32_t ESP_RxOverflow(void)
{
    return s_espRx.overflow;
}

uint32_t ESP_RxCount(void)
{
    return s_espRxCount;
}
