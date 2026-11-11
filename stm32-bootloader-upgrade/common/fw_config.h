#ifndef FW_CONFIG_H
#define FW_CONFIG_H

#include <stdint.h>

/* ==========================================================================
 * 全局配置：分区表 / 协议常量 / 外部 Flash 布局
 * 适用：STM32F103RCT6 (256KB Flash, 48KB RAM, 页大小 2KB)
 * 所有分区边界必须按 FW_FLASH_PAGE_SIZE 对齐
 * ========================================================================== */

#define FW_MAGIC                0x5A5AA5A5U

/* ---------- 芯片 Flash 参数 ----------
 * 注意：不能叫 FLASH_PAGE_SIZE，HAL 库在 stm32f1xx_hal_flash_ex.h 里已定义同名宏 */
#define FW_FLASH_BASE           0x08000000U
#define FW_FLASH_TOTAL_SIZE     (256U * 1024U)
#define FW_FLASH_PAGE_SIZE      2048U

/* ---------- 分区表 ---------- */
#define BOOT_START_ADDR         0x08000000U     /* Bootloader  24KB（HAL 工程留足余量） */
#define BOOT_SIZE               (24U * 1024U)

#define RESET_START_ADDR        0x08006000U     /* Reset 恢复出厂程序 24KB */
#define RESET_SIZE              (24U * 1024U)

#define APP_A_START_ADDR        0x0800C000U     /* APP A 区（运行区） 96KB */
#define APP_A_SIZE              (96U * 1024U)

#define APP_B_START_ADDR        0x08024000U     /* APP B 区（备份/中转） 96KB */
#define APP_B_SIZE              (96U * 1024U)

#define PARAM_START_ADDR        0x0803C000U     /* 参数区 16KB（预留，主用 AT24C02） */
#define PARAM_SIZE              (16U * 1024U)

/* ---------- RAM 边界（栈顶合法性检查用） ---------- */
#define RAM_START_ADDR          0x20000000U
#define RAM_END_ADDR            0x2000C000U     /* 48KB */

/* ---------- 固件与协议参数 ---------- */
#define FW_MAX_SIZE             APP_A_SIZE      /* 单区可容纳的最大固件 */
#define FW_CHUNK_SIZE           1024U           /* 建议单包数据长度 */
#define FW_PAYLOAD_MAX          (FW_CHUNK_SIZE + 8U)
#define FW_FRAME_OVERHEAD       7U              /* AA 55 cmd len(2) crc(2) */
#define FW_FRAME_MAX_LEN        (FW_PAYLOAD_MAX + FW_FRAME_OVERHEAD)

#define FW_FRAME_HEAD0          0xAAU
#define FW_FRAME_HEAD1          0x55U

#define FW_BOOT_VERSION         "BOOT v1.0"
#define FW_APP_VERSION          "APP  v1.3.0"

/* ---------- W25Q64 布局（8MB）---------- */
#define W25Q64_TOTAL_SIZE       (8U * 1024U * 1024U)
#define W25Q64_FW_BASE          0x000000U       /* 新固件暂存区，最大 1MB */
#define W25Q64_FW_MAX           (1024U * 1024U)
#define W25Q64_BACKUP_BASE      0x100000U       /* 预留：B 区镜像同步存储 */

/* ---------- 升级策略 ---------- */
#define FW_MAX_BOOT_TRY         3U              /* 连续启动失败次数上限，超过则回滚 */

#endif /* FW_CONFIG_H */
