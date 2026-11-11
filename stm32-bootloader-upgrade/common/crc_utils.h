#ifndef CRC_UTILS_H
#define CRC_UTILS_H

#include <stdint.h>

/* CRC16-MODBUS：用于单帧校验（帧内数据改动一位即可检出） */
uint16_t crc16_modbus(const uint8_t *data, uint32_t len);

/* CRC32（多项式 0xEDB88320 反射）：用于整包固件校验 */
uint32_t crc32_begin(void);
uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len);
uint32_t crc32_end(uint32_t crc);
uint32_t crc32_calc(const uint8_t *data, uint32_t len);

#endif /* CRC_UTILS_H */
