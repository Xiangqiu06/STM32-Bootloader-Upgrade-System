#include <string.h>
#include "bsp_uart_dma.h"
#include "fw_config.h"

/* 编译期护栏：一帧必须能完整放进 DMA 缓冲，否则大帧会在中途被绕回覆盖。
 * 若这里报错，去 bsp_uart_dma.h 把 UART_RX_DMA_SIZE 调大（或把 FW_CHUNK_SIZE 调小）。 */
#if (FW_FRAME_MAX_LEN > UART_RX_DMA_SIZE)
#error "UART_RX_DMA_SIZE must be >= FW_FRAME_MAX_LEN, otherwise large frames get corrupted"
#endif

static uint8_t  s_dma_buf[UART_RX_DMA_SIZE];
static uint16_t s_last_pos;
static UART_HandleTypeDef *s_huart;

static uint8_t           s_ring[UART_RING_SIZE];
static volatile uint16_t s_head;
static volatile uint16_t s_tail;

static void ring_push(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    for (i = 0U; i < len; i++)
    {
        uint16_t next = (uint16_t)((s_head + 1U) % UART_RING_SIZE);

        if (next == s_tail)
        {
            return;     /* 缓冲区满，丢弃后续数据（上位机ACK超时会重发） */
        }
        s_ring[s_head] = data[i];
        s_head = next;
    }
}

/* 把 DMA 缓冲里新增的字节搬进软件环形缓冲。
 * 唯一调用者是 bsp_uart_poll()（主循环上下文），因此不需要加锁。 */
static void dma_rx_drain(void)
{
    UART_HandleTypeDef *huart = s_huart;
    uint16_t            pos;

    if ((huart == 0) || (huart->hdmarx == 0))
    {
        return;
    }

    pos = (uint16_t)(UART_RX_DMA_SIZE - __HAL_DMA_GET_COUNTER(huart->hdmarx));
    if (pos == s_last_pos)
    {
        return;
    }

    if (pos > s_last_pos)
    {
        ring_push(&s_dma_buf[s_last_pos], (uint16_t)(pos - s_last_pos));
    }
    else
    {
        /* DMA 环形缓冲回卷 */
        ring_push(&s_dma_buf[s_last_pos], (uint16_t)(UART_RX_DMA_SIZE - s_last_pos));
        ring_push(&s_dma_buf[0], pos);
    }
    s_last_pos = pos;
}

void bsp_uart_rx_start(UART_HandleTypeDef *huart)
{
    s_huart    = huart;
    s_head     = 0U;
    s_tail     = 0U;
    s_last_pos = 0U;

    /* 只启动 DMA 循环接收，不开 IDLE 中断：
     * 数据什么时候被搬走由主循环 bsp_uart_poll() 决定，不依赖中断时机。 */
    (void)HAL_UART_Receive_DMA(huart, s_dma_buf, UART_RX_DMA_SIZE);
}

/* 【重要】在主循环里每个迭代都调用一次，这是数据进入环形缓冲的唯一入口。
 *
 * 为什么不用 IDLE 中断：实测中（CH340 向芯片交付字节的间隙抖动等原因）
 * 大帧结束后 IDLE 中断可能不触发，整帧 1039 字节滞留在 DMA 缓冲里，
 * 直到下一帧到来才被一起搬运——上位机表现就是"DATA 的 ACK 迟到 2 秒多，
 * 每包都超时重握手"。改成主循环轮询后，延迟从 2.4s 降到一圈主循环（µs 级），
 * 且不依赖任何中断触发条件，逻辑也更简单。
 *
 * 注意：本函数只能在主循环（线程上下文）调用，不要在中断里调用，
 * 否则与主循环并发修改 s_last_pos 会产生竞态。 */
void bsp_uart_poll(void)
{
    dma_rx_drain();
}

uint16_t bsp_uart_available(void)
{
    uint16_t head = s_head;
    uint16_t tail = s_tail;

    return (uint16_t)((head >= tail) ? (head - tail) : (UART_RING_SIZE - tail + head));
}

uint16_t bsp_uart_read(uint8_t *buf, uint16_t len)
{
    uint16_t n = 0U;

    while ((n < len) && (s_tail != s_head))
    {
        buf[n++] = s_ring[s_tail];
        s_tail = (uint16_t)((s_tail + 1U) % UART_RING_SIZE);
    }
    return n;
}
