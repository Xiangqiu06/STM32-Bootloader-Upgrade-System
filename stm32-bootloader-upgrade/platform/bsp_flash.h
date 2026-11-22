#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include <stdint.h>
#include "main.h"
#include "fw_config.h"

/* 内部 Flash IAP 封装（STM32F1 高密度：页 2KB，按半字编程） */
int      flash_erase_region(uint32_t addr, uint32_t len);           /* 自动向上对齐到页 */
int      flash_write(uint32_t addr, const uint8_t *buf, uint32_t len);
int      flash_read(uint32_t addr, uint8_t *buf, uint32_t len);
uint32_t flash_crc32(uint32_t addr, uint32_t len);
int      flash_is_blank(uint32_t addr, uint32_t len);               /* 全 0xFF 返回 1 */

#endif /* BSP_FLASH_H */
