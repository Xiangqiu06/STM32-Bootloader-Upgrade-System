#include "crc_utils.h"

/* CRC16-MODBUS，初值 0xFFFF，反射多项式 0xA001 */
uint16_t crc16_modbus(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t b;

    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i];
        for (b = 0; b < 8U; b++)
        {
            if (crc & 0x0001U)
            {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            }
            else
            {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

/* CRC32 标准实现（与 zlib.crc32 结果一致，便于上位机校验） */
uint32_t crc32_begin(void)
{
    return 0xFFFFFFFFU;
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint8_t b;

    for (i = 0; i < len; i++)
    {
        crc ^= (uint32_t)data[i];
        for (b = 0; b < 8U; b++)
        {
            if (crc & 1U)
            {
                crc = (crc >> 1) ^ 0xEDB88320U;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint32_t crc32_end(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFU;
}

uint32_t crc32_calc(const uint8_t *data, uint32_t len)
{
    return crc32_end(crc32_update(crc32_begin(), data, len));
}
