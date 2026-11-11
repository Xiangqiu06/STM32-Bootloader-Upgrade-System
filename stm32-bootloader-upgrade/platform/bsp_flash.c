#include <string.h>
#include "bsp_flash.h"
#include "crc_utils.h"

/* 新旧 F1 HAL 的写保护错误标志名不同：老版本 WRPRTERR，新版本 WRPERR */
#if defined(FLASH_FLAG_WRPRTERR)
#define FLASH_CLR_FLAGS()   __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR)
#else
#define FLASH_CLR_FLAGS()   __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR)
#endif

int flash_erase_region(uint32_t addr, uint32_t len)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_err = 0U;
    uint32_t start;
    uint32_t pages;
    HAL_StatusTypeDef st;

    if ((addr < FW_FLASH_BASE) || (len == 0U))
    {
        return -1;
    }
    if ((addr + len) > (FW_FLASH_BASE + FW_FLASH_TOTAL_SIZE))
    {
        return -1;
    }

    start = addr - (addr % FW_FLASH_PAGE_SIZE);
    pages = (addr + len - start + FW_FLASH_PAGE_SIZE - 1U) / FW_FLASH_PAGE_SIZE;

    HAL_FLASH_Unlock();
    FLASH_CLR_FLAGS();

    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = start;
    erase.NbPages     = pages;

    st = HAL_FLASHEx_Erase(&erase, &page_err);
    HAL_FLASH_Lock();

    return (st == HAL_OK) ? 0 : -1;
}

int flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    int ret = 0;

    if (((addr & 0x1U) != 0U) || (len == 0U))
    {
        return -1;      /* F1 必须半字对齐编程 */
    }

    HAL_FLASH_Unlock();
    FLASH_CLR_FLAGS();

    for (i = 0U; i < len; i += 2U)
    {
        uint16_t hw = (uint16_t)buf[i];

        if ((i + 1U) < len)
        {
            hw |= (uint16_t)((uint16_t)buf[i + 1U] << 8);
        }
        else
        {
            hw |= 0xFF00U;      /* 奇数字节用 0xFF 补齐，保持空片语义 */
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, (uint64_t)hw) != HAL_OK)
        {
            ret = -1;
            break;
        }
    }

    HAL_FLASH_Lock();
    return ret;
}

int flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    memcpy(buf, (const void *)addr, len);
    return 0;
}

uint32_t flash_crc32(uint32_t addr, uint32_t len)
{
    return crc32_calc((const uint8_t *)addr, len);
}

int flash_is_blank(uint32_t addr, uint32_t len)
{
    const uint32_t *p = (const uint32_t *)addr;
    uint32_t words = len / 4U;

    while (words-- > 0U)
    {
        if (*p++ != 0xFFFFFFFFU)
        {
            return 0;
        }
    }
    return 1;
}
