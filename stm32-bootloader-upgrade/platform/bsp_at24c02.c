#include "bsp_at24c02.h"

#define AT24C02_WRITE_DELAY_MS   6U     /* 页写内部擦写周期，手册 Typ 5ms */
#define I2C_DELAY_LOOPS          40U    /* 约 2~3us，对应 100~200kHz */

#define SCL_H()     HAL_GPIO_WritePin(AT24C02_SCL_PORT, AT24C02_SCL_PIN, GPIO_PIN_SET)
#define SCL_L()     HAL_GPIO_WritePin(AT24C02_SCL_PORT, AT24C02_SCL_PIN, GPIO_PIN_RESET)
#define SDA_H()     HAL_GPIO_WritePin(AT24C02_SDA_PORT, AT24C02_SDA_PIN, GPIO_PIN_SET)
#define SDA_L()     HAL_GPIO_WritePin(AT24C02_SDA_PORT, AT24C02_SDA_PIN, GPIO_PIN_RESET)
#define SDA_READ()  HAL_GPIO_ReadPin(AT24C02_SDA_PORT, AT24C02_SDA_PIN)

static void i2c_delay(void)
{
    volatile uint32_t i;

    for (i = 0U; i < I2C_DELAY_LOOPS; i++)
    {
        __NOP();
    }
}

static void i2c_start(void)
{
    SDA_H();
    SCL_H();
    i2c_delay();
    SDA_L();        /* SCL 高电平期间 SDA 下降沿 = 起始 */
    i2c_delay();
    SCL_L();
    i2c_delay();
}

static void i2c_stop(void)
{
    SDA_L();
    i2c_delay();
    SCL_H();
    i2c_delay();
    SDA_H();        /* SCL 高电平期间 SDA 上升沿 = 停止 */
    i2c_delay();
}

/* 返回 0 表示收到 ACK */
static int i2c_write_byte(uint8_t byte)
{
    uint8_t i;
    int ack;

    for (i = 0U; i < 8U; i++)
    {
        if (byte & 0x80U)
        {
            SDA_H();
        }
        else
        {
            SDA_L();
        }
        i2c_delay();
        SCL_H();
        i2c_delay();
        SCL_L();
        i2c_delay();
        byte = (uint8_t)(byte << 1);
    }

    SDA_H();        /* 释放 SDA，读从机应答 */
    i2c_delay();
    SCL_H();
    i2c_delay();
    ack = (SDA_READ() == GPIO_PIN_RESET) ? 0 : -1;
    SCL_L();
    i2c_delay();

    return ack;
}

static uint8_t i2c_read_byte(uint8_t send_ack)
{
    uint8_t i;
    uint8_t byte = 0U;

    SDA_H();        /* 释放 SDA，由从机驱动 */

    for (i = 0U; i < 8U; i++)
    {
        i2c_delay();
        SCL_H();
        i2c_delay();
        byte = (uint8_t)(byte << 1);
        if (SDA_READ() == GPIO_PIN_SET)
        {
            byte |= 0x01U;
        }
        SCL_L();
        i2c_delay();
    }

    if (send_ack != 0U)
    {
        SDA_L();    /* ACK */
    }
    else
    {
        SDA_H();    /* NACK */
    }
    i2c_delay();
    SCL_H();
    i2c_delay();
    SCL_L();
    i2c_delay();
    SDA_H();

    return byte;
}

int at24c02_read(uint16_t mem_addr, uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if (((uint32_t)mem_addr + len) > AT24C02_TOTAL_SIZE)
    {
        return -1;
    }

    i2c_start();
    if (i2c_write_byte(AT24C02_I2C_ADDR) != 0)
    {
        i2c_stop();
        return -2;
    }
    if (i2c_write_byte((uint8_t)mem_addr) != 0)
    {
        i2c_stop();
        return -2;
    }

    /* 重复起始 + 读地址 */
    i2c_start();
    if (i2c_write_byte((uint8_t)(AT24C02_I2C_ADDR | 0x01U)) != 0)
    {
        i2c_stop();
        return -2;
    }

    for (i = 0U; i < len; i++)
    {
        buf[i] = i2c_read_byte((i + 1U < len) ? 1U : 0U);
    }

    i2c_stop();
    return 0;
}

int at24c02_write(uint16_t mem_addr, const uint8_t *buf, uint16_t len)
{
    uint16_t done = 0U;

    if (((uint32_t)mem_addr + len) > AT24C02_TOTAL_SIZE)
    {
        return -1;
    }

    /* 必须按 8 字节页拆分，跨页写入会回卷覆盖本页开头 */
    while (done < len)
    {
        uint16_t cur       = (uint16_t)(mem_addr + done);
        uint16_t page_left = (uint16_t)(AT24C02_PAGE_SIZE - (cur % AT24C02_PAGE_SIZE));
        uint16_t n         = (uint16_t)((len - done) < page_left ? (len - done) : page_left);
        uint16_t i;

        i2c_start();
        if (i2c_write_byte(AT24C02_I2C_ADDR) != 0)
        {
            i2c_stop();
            return -2;
        }
        if (i2c_write_byte((uint8_t)cur) != 0)
        {
            i2c_stop();
            return -2;
        }
        for (i = 0U; i < n; i++)
        {
            if (i2c_write_byte(buf[done + i]) != 0)
            {
                i2c_stop();
                return -2;
            }
        }
        i2c_stop();

        HAL_Delay(AT24C02_WRITE_DELAY_MS);
        done = (uint16_t)(done + n);
    }
    return 0;
}

int at24c02_self_test(void)
{
    uint8_t  pattern[4] = {0x5AU, 0xA5U, 0x3CU, 0xC3U};
    uint8_t  readback[4];
    uint16_t test_addr  = AT24C02_TOTAL_SIZE - 4U;   /* 用最后 4 字节，避开参数区 */

    if (at24c02_write(test_addr, pattern, 4U) != 0)
    {
        return -1;
    }
    if (at24c02_read(test_addr, readback, 4U) != 0)
    {
        return -2;
    }
    if ((readback[0] != pattern[0]) || (readback[1] != pattern[1]) ||
        (readback[2] != pattern[2]) || (readback[3] != pattern[3]))
    {
        return -3;
    }
    return 0;
}
