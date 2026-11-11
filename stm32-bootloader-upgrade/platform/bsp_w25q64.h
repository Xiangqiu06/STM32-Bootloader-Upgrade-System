#ifndef BSP_W25Q64_H
#define BSP_W25Q64_H

#include <stdint.h>
#include "main.h"

/* W25Q64：8MB SPI Flash，扇区 4KB / 块 64KB / 页 256B
 *
 * 野火 F103-MINI 板接线（原理图已核对）：
 *   SPI1_SCK=PA5  SPI1_MISO=PA6  SPI1_MOSI=PA7  FLASH_CS=PA4
 *   TF 卡与 W25Q64 共用 SPI1，TF_CS=PA8，必须保持高电平（不选中）
 *   W25Q64 的 WP#/HOLD# 已由板上 4.7K 上拉到 3V3
 */
#define W25Q_SPI        hspi1
#define W25Q_CS_PORT    GPIOA
#define W25Q_CS_PIN     GPIO_PIN_4

/* 板载 TF 卡片选，若你的板子没接 TF 卡可注释掉下面两行 */
#define W25Q_TF_CS_PORT GPIOA
#define W25Q_TF_CS_PIN  GPIO_PIN_8

#define W25Q_CS_LOW()   HAL_GPIO_WritePin(W25Q_CS_PORT, W25Q_CS_PIN, GPIO_PIN_RESET)
#define W25Q_CS_HIGH()  HAL_GPIO_WritePin(W25Q_CS_PORT, W25Q_CS_PIN, GPIO_PIN_SET)

#define W25Q_PAGE_SIZE      256U
#define W25Q_SECTOR_SIZE    4096U
#define W25Q_BLOCK_SIZE     65536U

extern SPI_HandleTypeDef hspi1;

void     w25q_board_init(void);                 /* 片选全部置高，上电先调用 */
uint32_t w25q_read_id(void);                    /* 返回 0xEF4017 表示 W25Q64 */
int      w25q_read(uint32_t addr, uint8_t *buf, uint32_t len);
int      w25q_erase_region(uint32_t addr, uint32_t len);   /* 按 4KB 扇区擦除 */
int      w25q_write_no_erase(uint32_t addr, const uint8_t *buf, uint32_t len); /* 仅按页编程 */
uint32_t w25q_crc32(uint32_t addr, uint32_t len);

#endif /* BSP_W25Q64_H */
