#include "bsp_w25q64.h"
#include "crc_utils.h"

#define W25Q_CMD_WRITE_ENABLE   0x06U
#define W25Q_CMD_READ_STATUS    0x05U
#define W25Q_CMD_READ_DATA      0x03U
#define W25Q_CMD_PAGE_PROGRAM   0x02U
#define W25Q_CMD_SECTOR_ERASE   0x20U
#define W25Q_CMD_BLOCK_ERASE    0xD8U
#define W25Q_CMD_CHIP_ERASE     0xC7U
#define W25Q_CMD_JEDEC_ID       0x9FU

#define W25Q_TIMEOUT_MS         1000U

static uint8_t s_txbuf[W25Q_PAGE_SIZE + 4U];
static uint8_t s_rxbuf[W25Q_PAGE_SIZE + 4U];

static int w25q_wait_busy(uint32_t timeout_ms)
{
    uint8_t cmd = W25Q_CMD_READ_STATUS;
    uint8_t st  = 0xFFU;

    do
    {
        W25Q_CS_LOW();
        HAL_SPI_Transmit(&W25Q_SPI, &cmd, 1U, W25Q_TIMEOUT_MS);
        HAL_SPI_Receive(&W25Q_SPI, &st, 1U, W25Q_TIMEOUT_MS);
        W25Q_CS_HIGH();

        if ((st & 0x01U) == 0U)
        {
            return 0;
        }
        HAL_Delay(1U);
    } while (timeout_ms-- > 0U);

    return -1;
}

static void w25q_write_enable(void)
{
    uint8_t cmd = W25Q_CMD_WRITE_ENABLE;

    w25q_wait_busy(200U);
    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI, &cmd, 1U, W25Q_TIMEOUT_MS);
    W25Q_CS_HIGH();
}

void w25q_board_init(void)
{
    /* 先把两路片选都置高，否则 TF 卡与 W25Q64 会在 MISO 上打架，读 ID 会得到 0xFFFFFF */
    W25Q_CS_HIGH();
#ifdef W25Q_TF_CS_PORT
    HAL_GPIO_WritePin(W25Q_TF_CS_PORT, W25Q_TF_CS_PIN, GPIO_PIN_SET);
#endif
    HAL_Delay(5U);
}

uint32_t w25q_read_id(void)
{
    uint8_t cmd = W25Q_CMD_JEDEC_ID;
    uint8_t id[3] = {0U, 0U, 0U};

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI, &cmd, 1U, W25Q_TIMEOUT_MS);
    HAL_SPI_Receive(&W25Q_SPI, id, 3U, W25Q_TIMEOUT_MS);
    W25Q_CS_HIGH();

    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | (uint32_t)id[2];
}

int w25q_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint8_t hdr[4];

    while (len > 0U)
    {
        uint32_t n = (len > W25Q_PAGE_SIZE) ? W25Q_PAGE_SIZE : len;

        hdr[0] = W25Q_CMD_READ_DATA;
        hdr[1] = (uint8_t)(addr >> 16);
        hdr[2] = (uint8_t)(addr >> 8);
        hdr[3] = (uint8_t)(addr);

        W25Q_CS_LOW();
        HAL_SPI_Transmit(&W25Q_SPI, hdr, 4U, W25Q_TIMEOUT_MS);
        if (HAL_SPI_Receive(&W25Q_SPI, buf, (uint16_t)n, W25Q_TIMEOUT_MS) != HAL_OK)
        {
            W25Q_CS_HIGH();
            return -1;
        }
        W25Q_CS_HIGH();

        addr += n;
        buf  += n;
        len  -= n;
    }
    return 0;
}

int w25q_erase_region(uint32_t addr, uint32_t len)
{
    uint32_t start = addr - (addr % W25Q_SECTOR_SIZE);
    uint32_t end   = addr + len;
    uint32_t s;

    for (s = start; s < end; s += W25Q_SECTOR_SIZE)
    {
        uint8_t cmd[4];

        w25q_write_enable();
        cmd[0] = W25Q_CMD_SECTOR_ERASE;
        cmd[1] = (uint8_t)(s >> 16);
        cmd[2] = (uint8_t)(s >> 8);
        cmd[3] = (uint8_t)(s);

        W25Q_CS_LOW();
        HAL_SPI_Transmit(&W25Q_SPI, cmd, 4U, W25Q_TIMEOUT_MS);
        W25Q_CS_HIGH();

        if (w25q_wait_busy(500U) != 0)
        {
            return -1;
        }
    }
    return 0;
}

int w25q_write_no_erase(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t done = 0U;

    while (done < len)
    {
        uint32_t cur       = addr + done;
        uint32_t page_left = W25Q_PAGE_SIZE - (cur % W25Q_PAGE_SIZE);
        uint32_t n         = (len - done) < page_left ? (len - done) : page_left;

        w25q_write_enable();

        s_txbuf[0] = W25Q_CMD_PAGE_PROGRAM;
        s_txbuf[1] = (uint8_t)(cur >> 16);
        s_txbuf[2] = (uint8_t)(cur >> 8);
        s_txbuf[3] = (uint8_t)(cur);
        {
            uint32_t i;
            for (i = 0U; i < n; i++)
            {
                s_txbuf[4U + i] = buf[done + i];
            }
        }

        W25Q_CS_LOW();
        if (HAL_SPI_Transmit(&W25Q_SPI, s_txbuf, (uint16_t)(n + 4U), W25Q_TIMEOUT_MS) != HAL_OK)
        {
            W25Q_CS_HIGH();
            return -1;
        }
        W25Q_CS_HIGH();

        if (w25q_wait_busy(100U) != 0)
        {
            return -1;
        }
        done += n;
    }
    return 0;
}

uint32_t w25q_crc32(uint32_t addr, uint32_t len)
{
    uint32_t crc = crc32_begin();

    while (len > 0U)
    {
        uint32_t n = (len > W25Q_PAGE_SIZE) ? W25Q_PAGE_SIZE : len;

        if (w25q_read(addr, s_rxbuf, n) != 0)
        {
            return 0xFFFFFFFFU;
        }
        crc = crc32_update(crc, s_rxbuf, n);

        addr += n;
        len  -= n;
    }
    return crc32_end(crc);
}
