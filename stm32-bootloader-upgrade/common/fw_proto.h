#ifndef FW_PROTO_H
#define FW_PROTO_H

#include <stdint.h>
#include "fw_config.h"

/* ==========================================================================
 * 固件升级协议（链路无关：串口 / CAN / LoRa / 4G-MQTT 均复用）
 *
 * 帧格式： AA 55 | CMD | LEN(2, 小端) | PAYLOAD(LEN) | CRC16(2, 小端)
 *          CRC16 覆盖 CMD + LEN + PAYLOAD
 * ========================================================================== */

#define FW_CMD_START        0x01U   /* 上位机 -> 设备：升级开始 */
#define FW_CMD_DATA         0x02U   /* 上位机 -> 设备：数据分片 */
#define FW_CMD_END          0x03U   /* 上位机 -> 设备：升级结束 */
#define FW_CMD_ACK          0x81U   /* 设备 -> 上位机：应答 */

/* 应答状态码 */
#define FW_STATUS_OK            0U
#define FW_STATUS_PARAM_ERR     1U
#define FW_STATUS_ERASE_ERR     2U
#define FW_STATUS_WRITE_ERR     3U
#define FW_STATUS_CRC_ERR       4U
#define FW_STATUS_STATE_ERR     5U
#define FW_STATUS_SIZE_ERR      6U

/* 解析结果 */
#define FW_PARSE_MORE           0
#define FW_PARSE_OK             1
#define FW_PARSE_BAD_CRC      (-1)
#define FW_PARSE_BAD_LEN      (-2)

/* ---------- payload 结构（小端，紧凑排列） ---------- */
#pragma pack(push, 1)

typedef struct
{
    uint32_t fw_size;       /* 固件总字节数 */
    uint32_t fw_crc32;      /* 整包 CRC32 */
    uint16_t fw_version;    /* 版本号，如 0x0100 表示 v1.0 */
    uint16_t chunk_size;    /* 上位机使用的分片长度 */
} fw_start_payload_t;

typedef struct
{
    uint16_t seq;           /* 包序号，从 0 开始 */
    uint32_t offset;        /* 相对固件起始的偏移 */
    uint16_t len;           /* 本包数据长度（<= chunk_size） */
} fw_data_header_t;

typedef struct
{
    uint32_t fw_size;
    uint32_t fw_crc32;
} fw_end_payload_t;

typedef struct
{
    uint8_t  cmd;           /* 被应答的命令 */
    uint8_t  status;        /* FW_STATUS_xxx */
    uint16_t seq;           /* 对应包序号，START/END 时为 0 */
} fw_ack_payload_t;

#pragma pack(pop)

/* ---------- 帧结构 ---------- */
typedef struct
{
    uint8_t        cmd;
    uint16_t       len;
    const uint8_t *payload;
} fw_frame_t;

/* 组帧：返回帧总长，0 表示失败 */
uint16_t fw_frame_build(uint8_t cmd, const uint8_t *payload, uint16_t plen,
                        uint8_t *out, uint16_t out_cap);

/* 流式解析器：逐字节喂入，抗粘包/半包/丢字节 */
typedef struct
{
    uint8_t  state;
    uint16_t idx;
    uint16_t plen;
    uint8_t  buf[FW_FRAME_MAX_LEN];
} fw_parser_t;

void fw_parser_init(fw_parser_t *p);
int  fw_parser_push(fw_parser_t *p, uint8_t byte, fw_frame_t *out);

#endif /* FW_PROTO_H */
