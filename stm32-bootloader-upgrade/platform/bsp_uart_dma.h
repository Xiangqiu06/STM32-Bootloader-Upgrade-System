#ifndef BSP_UART_DMA_H
#define BSP_UART_DMA_H

#include <stdint.h>
#include "main.h"

/* UART + DMA + 主循环轮询：不定长数据接收，数据先入环形缓冲区，
 * 主循环里调用 bsp_uart_read() 取数据，避免在中断里做 Flash/EEPROM 操作 */

/* 【必须满足】UART_RX_DMA_SIZE >= FW_FRAME_MAX_LEN（当前 1039 字节）
 *
 * 原因：DMA 工作在 Circular 模式，写满会绕回覆盖最老的数据。
 * 主循环轮询是"事后查看"，如果一帧比缓冲还长，等到去搬的时候前半部分
 * 已经被后半部分覆盖，只剩尾部残片。
 * 表现为：长度小于缓冲的帧（如 START 19 字节）正常，DATA 大帧无 ACK 超时。
 *
 * FW_FRAME_MAX_LEN = FW_PAYLOAD_MAX(1032) + FW_FRAME_OVERHEAD(7) = 1039
 * 这里取 2048，给一帧留足余量（RAM 48KB，可接受）。
 * 如果以后把 FW_CHUNK_SIZE 调大，这个值必须同步跟着调大。
 */
#define UART_RX_DMA_SIZE    2048U
#define UART_RING_SIZE      4096U

void     bsp_uart_rx_start(UART_HandleTypeDef *huart);
void     bsp_uart_poll(void);                            /* 主循环每个迭代调用一次，唯一的搬运入口 */
uint16_t bsp_uart_read(uint8_t *buf, uint16_t len);
uint16_t bsp_uart_available(void);

#endif /* BSP_UART_DMA_H */
