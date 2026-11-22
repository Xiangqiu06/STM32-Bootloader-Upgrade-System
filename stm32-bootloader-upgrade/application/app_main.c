#include <string.h>
#include "main.h"
#include "app_main.h"
#include "fw_config.h"
#include "fw_param.h"
#include "fw_update.h"
#include "bsp_uart_dma.h"
#include "bsp_w25q64.h"
#include "bsp_led.h"

static void app_tx(const uint8_t *d, uint16_t n)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)d, n, 200U);
}

static void app_log(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 200U);
}

static void app_log_num(uint32_t v)
{
    char buf[12];
    char tmp[12];
    int  i = 0;
    int  j = 0;

    if (v == 0U)
    {
        buf[i++] = '0';
    }
    else
    {
        while ((v > 0U) && (j < 10))
        {
            tmp[j++] = (char)('0' + (v % 10U));
            v /= 10U;
        }
        while (j > 0)
        {
            buf[i++] = tmp[--j];
        }
    }
    buf[i] = '\0';
    app_log(buf);
}

void app_run(void)
{
    static uint8_t  rx[128];
    uint32_t        tick_led = 0U;
    uint32_t        last_pct = 0xFFFFFFFFU;
    fw_param_t      param;

    /* 向量表偏移：保险起见再设一次（正常应在 main() 最开始设置） */
    SCB->VTOR = APP_A_START_ADDR;

    app_log("\r\n");
    app_log("[APP] " FW_APP_VERSION " running @0x0800C000\r\n");

    if (fw_param_load(&param) == 0)
    {
        app_log("[APP] fw_version=");
        app_log_num((param.fw_version >> 8) & 0xFFU);
        app_log(".");
        app_log_num(param.fw_version & 0xFFU);
        app_log("\r\n");
    }

    /* 到这里说明 APP 正常启动，清零启动尝试计数，避免 Bootloader 误回滚 */
    (void)fw_param_confirm_boot();

    w25q_board_init();              /* 片选全部拉高，再确认外挂 Flash 在位 */
    (void)w25q_read_id();
    fw_update_init(app_tx, app_log);
    bsp_uart_rx_start(&huart1);

    app_log("[APP] ready, send firmware via UART to upgrade\r\n");

    while (1)
    {
        bsp_uart_poll();    /* 轮询搬运 DMA 新数据（本工程不用 IDLE 中断） */

        uint16_t n = bsp_uart_read(rx, (uint16_t)sizeof(rx));

        if (n > 0U)
        {
            fw_update_feed(rx, n);
        }

        /* 升级进度提示（每 10% 打印一次） */
        if (fw_update_state() == FW_ST_RECEIVING)
        {
            uint32_t pct = fw_update_progress() / 10U;

            if (pct != last_pct)
            {
                last_pct = pct;
                app_log("[APP] progress ");
                app_log_num(pct * 10U);
                app_log("%\r\n");
            }
        }
        else
        {
            last_pct = 0xFFFFFFFFU;
        }

        /* 运行指示灯：500ms 翻转，可与 Bootloader 阶段区分 */
        if ((HAL_GetTick() - tick_led) >= 1000U)
        {
            tick_led = HAL_GetTick();
            LED_TOGGLE();   /* 500ms 翻转，可与 Bootloader 阶段区分 */
        }
    }
}
