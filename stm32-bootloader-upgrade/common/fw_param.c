#include <string.h>
#include "fw_param.h"
#include "crc_utils.h"
#include "bsp_at24c02.h"

static uint32_t param_crc(const fw_param_t *p)
{
    return crc32_calc((const uint8_t *)p, (uint32_t)(sizeof(fw_param_t) - sizeof(uint32_t)));
}

void fw_param_default(fw_param_t *p)
{
    memset(p, 0, sizeof(fw_param_t));
    p->magic      = FW_MAGIC;
    p->fw_version = 0x0100U;
    p->update_req = 0U;
    p->boot_try   = 0U;
    p->crc        = param_crc(p);
}

static int param_try_load(uint16_t addr, fw_param_t *p)
{
    if (at24c02_read(addr, (uint8_t *)p, (uint16_t)sizeof(fw_param_t)) != 0)
    {
        return -1;
    }
    if ((p->magic != FW_MAGIC) || (p->crc != param_crc(p)))
    {
        return -1;
    }
    return 0;
}

int fw_param_load(fw_param_t *p)
{
    if (param_try_load(PARAM_PRIMARY_ADDR, p) == 0)
    {
        return 0;
    }
    if (param_try_load(PARAM_BACKUP_ADDR, p) == 0)
    {
        (void)fw_param_save(p);     /* 主份损坏，用备份修复 */
        return 0;
    }
    fw_param_default(p);
    return -1;
}

int fw_param_save(const fw_param_t *p)
{
    fw_param_t tmp = *p;
    tmp.magic = FW_MAGIC;
    tmp.crc   = param_crc(&tmp);

    if (at24c02_write(PARAM_PRIMARY_ADDR, (const uint8_t *)&tmp, (uint16_t)sizeof(tmp)) != 0)
    {
        return -1;
    }
    if (at24c02_write(PARAM_BACKUP_ADDR, (const uint8_t *)&tmp, (uint16_t)sizeof(tmp)) != 0)
    {
        return -1;
    }
    return 0;
}

int fw_param_confirm_boot(void)
{
    fw_param_t p;

    if (fw_param_load(&p) != 0)
    {
        return -1;
    }
    if (p.boot_try != 0U)
    {
        p.boot_try = 0U;
        return fw_param_save(&p);
    }
    return 0;
}
