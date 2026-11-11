#ifndef FW_PARAM_H
#define FW_PARAM_H

#include <stdint.h>
#include "fw_config.h"

/* 升级参数 / 标志：存放于 AT24C02，主备份双份存储，任一份损坏可回退 */
#define PARAM_PRIMARY_ADDR      0x00U
#define PARAM_BACKUP_ADDR       0x20U

#pragma pack(push, 1)
typedef struct
{
    uint32_t magic;         /* FW_MAGIC */
    uint16_t fw_version;
    uint8_t  update_req;    /* 1 = Bootloader 需要搬运新固件 */
    uint8_t  boot_try;      /* 连续启动尝试次数，APP 运行成功后清零 */
    uint32_t fw_size;       /* 待搬运固件字节数 */
    uint32_t fw_crc32;      /* 待搬运固件 CRC32 */
    uint32_t reserved;
    uint32_t crc;           /* 以上字段的 CRC32 */
} fw_param_t;
#pragma pack(pop)

void fw_param_default(fw_param_t *p);
int  fw_param_load(fw_param_t *p);          /* 0 = 读到有效参数，-1 = 用了默认值 */
int  fw_param_save(const fw_param_t *p);
int  fw_param_confirm_boot(void);           /* APP 启动成功后调用：清零 boot_try */

#endif /* FW_PARAM_H */
