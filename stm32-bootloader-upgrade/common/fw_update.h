#ifndef FW_UPDATE_H
#define FW_UPDATE_H

#include <stdint.h>
#include "fw_config.h"
#include "fw_proto.h"

/* 升级状态机：接收分片 -> 写 W25Q64 暂存 -> 整包校验 -> 置标志 -> 复位
 * APP 和 Reset 恢复出厂程序共用该模块，只是"数据从哪条链路来"不同
 */

typedef enum
{
    FW_ST_IDLE = 0,
    FW_ST_RECEIVING,
    FW_ST_VERIFY,
    FW_ST_DONE,
    FW_ST_ERROR
} fw_update_state_t;

typedef void (*fw_tx_fn)(const uint8_t *data, uint16_t len);
typedef void (*fw_log_fn)(const char *s);

void              fw_update_init(fw_tx_fn tx, fw_log_fn log);
void              fw_update_feed(const uint8_t *data, uint16_t len);
void              fw_update_abort(void);
fw_update_state_t fw_update_state(void);
uint32_t          fw_update_progress(void);     /* 0 ~ 100 */

#endif /* FW_UPDATE_H */
