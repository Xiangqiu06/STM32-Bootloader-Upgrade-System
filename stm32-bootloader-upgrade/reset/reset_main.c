#include <string.h>
#include "main.h"
#include "reset_main.h"
#include "fw_config.h"
#include "fw_param.h"
#include "fw_update.h"
#include "bsp_uart_dma.h"
#include "bsp_w25q64.h"
#include "bsp_led.h"

/* 恢复出厂程序：只做一件事——通过串口把新固件收进 W25Q64，
 * 然后置升级标志、复位，由 Bootloader 搬到 A 区。
 * 因为 A 区已经不可用，这条路径必须尽量简单、少依赖。
 */

static void rst_tx(const uint8_t *d, uint16_t n)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)d, n, 200U);
}

static void rst_log(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 200U);
}

void reset_run(void)
{
    static uint8_t rx[128];
    uint32_t       tick_led = 0U;
    uint32_t       idle_ms  = 0U;
    uint32_t       last_rx  = 0U;

    SCB->VTOR = RESET_START_ADDR;

    rst_log("\r\n");
    rst_log("[RESET] recovery mode, A area invalid or upgrade requested\r\n");
    rst_log("[RESET] waiting firmware over UART @115200 ...\r\n");

    w25q_board_init();
    if (w25q_read_id() != 0xEF4017U)
    {
        rst_log("[RESET] W25Q64 not found! check SPI wiring\r\n");
    }

    fw_update_init(rst_tx, rst_log);
    bsp_uart_rx_start(&huart1);

    while (1)
    {
        bsp_uart_poll();    /* 轮询搬运 DMA 新数据（本工程不用 IDLE 中断） */

        uint16_t n = bsp_uart_read(rx, (uint16_t)sizeof(rx));

        if (n > 0U)
        {
            last_rx = HAL_GetTick();
            fw_update_feed(rx, n);
        }

        /* 200ms 慢闪：表示等待固件中 */
        if ((HAL_GetTick() - tick_led) >= 200U)
        {
            tick_led = HAL_GetTick();
            LED_TOGGLE();   /* 200ms 慢闪：表示等待固件中 */

            idle_ms = HAL_GetTick() - last_rx;
            if (idle_ms > 30000U)
            {
                /* 30 秒没数据，提示一次，方便现场排查 */
                rst_log("[RESET] still waiting... (check TX/RX and baudrate)\r\n");
                last_rx = HAL_GetTick();
            }
        }
    }
}
