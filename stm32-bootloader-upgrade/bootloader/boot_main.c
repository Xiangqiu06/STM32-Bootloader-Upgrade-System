#include <string.h>
#include "main.h"
#include "boot_main.h"
#include "boot_jump.h"
#include "fw_config.h"
#include "fw_param.h"
#include "bsp_flash.h"
#include "bsp_w25q64.h"
#include "bsp_at24c02.h"
#include "bsp_led.h"
#include "crc_utils.h"

#define COPY_CHUNK          1024U

static fw_param_t s_param;
static uint8_t    s_buf[COPY_CHUNK];

/* ---------------------------- 日志（不依赖 printf） ---------------------------- */
static void boot_log(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 200U);
}

static void boot_log_num(uint32_t v)
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
    boot_log(buf);
}

static void boot_log_hex(uint32_t v)
{
    char buf[11];
    int  i;
    const char *digits = "0123456789ABCDEF";

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++)
    {
        buf[2 + i] = digits[(v >> ((7 - i) * 4)) & 0xFU];
    }
    buf[10] = '\0';
    boot_log(buf);
}

/* --------------------------- 分区搬运 / 备份 / 回滚 --------------------------- */
/* src_external = 1：源为 W25Q64；0：源为内部 Flash */
static int flash_copy(uint32_t dst, uint32_t src, uint32_t len, uint8_t src_external)
{
    uint32_t done = 0U;

    if (flash_erase_region(dst, len) != 0)
    {
        return -1;
    }

    while (done < len)
    {
        uint32_t remain = len - done;
        uint32_t n      = (remain > COPY_CHUNK) ? COPY_CHUNK : remain;

        if (n != remain)
        {
            n &= ~0x1U;         /* 非末块保持半字对齐 */
        }

        if (src_external)
        {
            if (w25q_read(src + done, s_buf, n) != 0)
            {
                return -2;
            }
        }
        else
        {
            memcpy(s_buf, (const void *)(src + done), n);
        }

        if (flash_write(dst + done, s_buf, n) != 0)
        {
            return -3;
        }
        done += n;
    }
    return 0;
}

static int rollback_from_b(void)
{
    if (!boot_app_valid(APP_B_START_ADDR))
    {
        boot_log("[BOOT] B area invalid, rollback skipped\r\n");
        return -1;
    }
    boot_log("[BOOT] rollback B -> A ...\r\n");
    if (flash_copy(APP_A_START_ADDR, APP_B_START_ADDR, FW_MAX_SIZE, 0U) != 0)
    {
        return -1;
    }
    return boot_app_valid(APP_A_START_ADDR) ? 0 : -1;
}

/* ------------------------------- 升级搬运流程 ------------------------------- */
static void handle_update(uint32_t flash_id_ok)
{
    uint32_t crc;

    boot_log("[BOOT] update request, size=");
    boot_log_num(s_param.fw_size);
    boot_log("\r\n");

    if ((flash_id_ok == 0U) || (s_param.fw_size == 0U) || (s_param.fw_size > FW_MAX_SIZE))
    {
        boot_log("[BOOT] invalid request, cleared\r\n");
        s_param.update_req = 0U;
        (void)fw_param_save(&s_param);
        return;
    }

    /* 1. 校验 W25Q64 里的暂存固件 */
    crc = w25q_crc32(W25Q64_FW_BASE, s_param.fw_size);
    if (crc != s_param.fw_crc32)
    {
        boot_log("[BOOT] staged image CRC error, cleared\r\n");
        s_param.update_req = 0U;
        (void)fw_param_save(&s_param);
        return;
    }

    /* 2. 备份当前 A 区到 B 区（用于启动失败回滚） */
    if (boot_app_valid(APP_A_START_ADDR))
    {
        boot_log("[BOOT] backup A -> B ...\r\n");
        if (flash_copy(APP_B_START_ADDR, APP_A_START_ADDR, FW_MAX_SIZE, 0U) != 0)
        {
            boot_log("[BOOT] backup failed (continue)\r\n");
        }
    }

    /* 3. 搬运到 A 区并校验 */
    boot_log("[BOOT] migrate W25Q64 -> A ...\r\n");
    if ((flash_copy(APP_A_START_ADDR, W25Q64_FW_BASE, s_param.fw_size, 1U) != 0) ||
        (flash_crc32(APP_A_START_ADDR, s_param.fw_size) != s_param.fw_crc32))
    {
        boot_log("[BOOT] migrate failed, try rollback\r\n");
        (void)rollback_from_b();
        s_param.update_req = 0U;
        (void)fw_param_save(&s_param);
        return;
    }

    s_param.update_req = 0U;
    s_param.boot_try   = 0U;
    (void)fw_param_save(&s_param);
    boot_log("[BOOT] update success\r\n");
}

/* ---------------------------------- 主流程 ---------------------------------- */
void boot_run(void)
{
    uint32_t flash_id;
    uint8_t  flash_id_ok;
    uint8_t  try_jump;

    HAL_Delay(50);      /* 等电源稳定，同时给串口留出抓日志的窗口 */

    boot_log("\r\n----------------------------\r\n");
    boot_log("[BOOT] " FW_BOOT_VERSION "\r\n");

    w25q_board_init();      /* FLASH_CS / TF_CS 全部拉高 */
    flash_id    = w25q_read_id();
    flash_id_ok = (flash_id == 0xEF4017U) ? 1U : 0U;
    if (flash_id_ok == 0U)
    {
        boot_log("[BOOT] W25Q64 id=");
        boot_log_hex(flash_id);
        boot_log(" (expect 0x00EF4017)\r\n");
    }

    if (fw_param_load(&s_param) != 0)
    {
        boot_log("[BOOT] param invalid, use default\r\n");
        (void)fw_param_save(&s_param);
    }

    /* 1. 处理升级请求 */
    if (s_param.update_req != 0U)
    {
        handle_update(flash_id_ok);
    }

    /* 2. 连续启动失败 -> 回滚 */
    try_jump = 1U;
    if (s_param.boot_try >= FW_MAX_BOOT_TRY)
    {
        boot_log("[BOOT] boot_try exceeded, rollback\r\n");
        if (rollback_from_b() == 0)
        {
            s_param.boot_try   = 0U;
            s_param.update_req = 0U;
            (void)fw_param_save(&s_param);
        }
        else
        {
            try_jump = 0U;      /* 回滚不了，直接进恢复出厂 */
        }
    }

    /* 3. 校验 A 区并跳转 */
    if ((try_jump != 0U) && boot_app_valid(APP_A_START_ADDR))
    {
        s_param.boot_try = (uint8_t)((s_param.boot_try < 255U) ? (s_param.boot_try + 1U) : 255U);
        (void)fw_param_save(&s_param);

        boot_log("[BOOT] jump to APP @0x0800C000\r\n");
        HAL_Delay(20);
        boot_jump_to(APP_A_START_ADDR);
    }

    /* 4. 兜底：进入恢复出厂程序 */
    boot_log("[BOOT] A area invalid, enter RESET\r\n");
    if (boot_app_valid(RESET_START_ADDR))
    {
        HAL_Delay(20);
        boot_jump_to(RESET_START_ADDR);
    }

    boot_log("[BOOT] RESET invalid too! stay in bootloader\r\n");
    while (1)
    {
        LED_TOGGLE();       /* 快闪表示彻底无可用固件 */
        HAL_Delay(100);
    }
}
