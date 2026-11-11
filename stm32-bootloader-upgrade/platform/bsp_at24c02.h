#ifndef BSP_AT24C02_H
#define BSP_AT24C02_H

#include <stdint.h>
#include "main.h"

/* ==========================================================================
 * 野火 F103-MINI 板载 AT24C02（256 字节，8 字节/页，地址 0x50）
 *
 * 注意：该板 EEPROM 的 SCL/SDA 接在 **PA2 / PA3**，而 F103 的硬件 I2C1 只能在
 * PB6/PB7（该板已被 LCD_D6/D7 占用），所以这里用 **软件模拟 I2C**。
 *
 * CubeMX 需要配置：
 *   PA2 -> GPIO_Output, Open Drain, Pull-up, High speed    （SCL）
 *   PA3 -> GPIO_Output, Open Drain, Pull-up, High speed    （SDA）
 * ========================================================================== */

#define AT24C02_I2C_ADDR        0xA0U       /* 0x50 << 1 */
#define AT24C02_PAGE_SIZE       8U
#define AT24C02_TOTAL_SIZE      256U

#define AT24C02_SCL_PORT        GPIOA
#define AT24C02_SCL_PIN         GPIO_PIN_2
#define AT24C02_SDA_PORT        GPIOA
#define AT24C02_SDA_PIN         GPIO_PIN_3

int at24c02_read(uint16_t mem_addr, uint8_t *buf, uint16_t len);
int at24c02_write(uint16_t mem_addr, const uint8_t *buf, uint16_t len);
int at24c02_self_test(void);    /* 写读回比对，返回 0 表示 EEPROM 正常 */

#endif /* BSP_AT24C02_H */
