#include <string.h>
#include "fw_update.h"
#include "fw_param.h"
#include "bsp_w25q64.h"
#include "bsp_flash.h"
#include "crc_utils.h"
#include "main.h"

static fw_update_state_t s_state;
static fw_parser_t       s_parser;
static fw_start_payload_t s_info;
static uint32_t          s_written_high;
static fw_tx_fn          s_tx;
static fw_log_fn         s_log;

static void up_log(const char *s)
{
    if (s_log != 0)
    {
        s_log(s);
    }
}

static void send_ack(uint8_t cmd, uint8_t status, uint16_t seq)
{
    uint8_t          frame[16U];    /* ACK 固定 11 字节，不需要大缓冲 */
    fw_ack_payload_t ack;
    uint16_t         n;

    ack.cmd    = cmd;
    ack.status = status;
    ack.seq    = seq;

    n = fw_frame_build(FW_CMD_ACK, (const uint8_t *)&ack, (uint16_t)sizeof(ack),
                       frame, (uint16_t)sizeof(frame));
    if ((n > 0U) && (s_tx != 0))
    {
        s_tx(frame, n);
    }
}

void fw_update_init(fw_tx_fn tx, fw_log_fn log)
{
    s_tx           = tx;
    s_log          = log;
    s_state        = FW_ST_IDLE;
    s_written_high = 0U;
    memset(&s_info, 0, sizeof(s_info));
    fw_parser_init(&s_parser);
}

void fw_update_abort(void)
{
    s_state = FW_ST_IDLE;
    s_written_high = 0U;
    fw_parser_init(&s_parser);
}

fw_update_state_t fw_update_state(void)
{
    return s_state;
}

uint32_t fw_update_progress(void)
{
    if ((s_info.fw_size == 0U) || (s_state != FW_ST_RECEIVING))
    {
        return (s_state == FW_ST_DONE) ? 100U : 0U;
    }
    return (s_written_high * 100U) / s_info.fw_size;
}

/* ------------------------------- 三阶段处理 ------------------------------- */
static void handle_start(const fw_frame_t *f)
{
    fw_start_payload_t req;

    if (f->len != sizeof(fw_start_payload_t))
    {
        send_ack(FW_CMD_START, FW_STATUS_PARAM_ERR, 0U);
        return;
    }
    memcpy(&req, f->payload, sizeof(req));

    if ((req.fw_size == 0U) || (req.fw_size > FW_MAX_SIZE) || (req.fw_size > W25Q64_FW_MAX))
    {
        up_log("[UP] fw_size invalid\r\n");
        send_ack(FW_CMD_START, FW_STATUS_SIZE_ERR, 0U);
        return;
    }
    if ((req.chunk_size == 0U) || (req.chunk_size > FW_CHUNK_SIZE))
    {
        send_ack(FW_CMD_START, FW_STATUS_PARAM_ERR, 0U);
        return;
    }

    up_log("[UP] start, size=");
    {
        char num[12];
        uint32_t v = req.fw_size;
        int i = 0;
        char tmp[12];
        int j = 0;
        while ((v > 0U) && (j < 10))
        {
            tmp[j++] = (char)('0' + (v % 10U));
            v /= 10U;
        }
        while (j > 0)
        {
            num[i++] = tmp[--j];
        }
        num[i] = '\0';
        up_log(num);
    }
    up_log(" bytes, erasing...\r\n");

    if (w25q_erase_region(W25Q64_FW_BASE, req.fw_size) != 0)
    {
        up_log("[UP] erase failed\r\n");
        s_state = FW_ST_ERROR;
        send_ack(FW_CMD_START, FW_STATUS_ERASE_ERR, 0U);
        return;
    }

    s_info         = req;
    s_written_high = 0U;
    s_state        = FW_ST_RECEIVING;
    up_log("[UP] ready\r\n");
    send_ack(FW_CMD_START, FW_STATUS_OK, 0U);
}

static void handle_data(const fw_frame_t *f)
{
    fw_data_header_t hdr;
    const uint8_t   *data;
    uint16_t         ack_seq;

    if ((f->len < sizeof(fw_data_header_t)) || (f->len > (sizeof(fw_data_header_t) + FW_CHUNK_SIZE)))
    {
        send_ack(FW_CMD_DATA, FW_STATUS_PARAM_ERR, 0U);
        return;
    }
    memcpy(&hdr, f->payload, sizeof(hdr));
    ack_seq = hdr.seq;

    if (s_state != FW_ST_RECEIVING)
    {
        send_ack(FW_CMD_DATA, FW_STATUS_STATE_ERR, ack_seq);
        return;
    }
    if ((hdr.len != (f->len - sizeof(fw_data_header_t))) ||
        ((hdr.offset + hdr.len) > s_info.fw_size) ||
        (hdr.len == 0U))
    {
        send_ack(FW_CMD_DATA, FW_STATUS_PARAM_ERR, ack_seq);
        return;
    }

    data = f->payload + sizeof(fw_data_header_t);
    if (w25q_write_no_erase(W25Q64_FW_BASE + hdr.offset, data, hdr.len) != 0)
    {
        send_ack(FW_CMD_DATA, FW_STATUS_WRITE_ERR, ack_seq);
        return;
    }

    if ((hdr.offset + hdr.len) > s_written_high)
    {
        s_written_high = hdr.offset + hdr.len;
    }
    send_ack(FW_CMD_DATA, FW_STATUS_OK, ack_seq);
}

static void handle_end(const fw_frame_t *f)
{
    fw_end_payload_t end;
    fw_param_t       param;
    uint32_t         crc;

    if (f->len != sizeof(fw_end_payload_t))
    {
        send_ack(FW_CMD_END, FW_STATUS_PARAM_ERR, 0U);
        return;
    }
    memcpy(&end, f->payload, sizeof(end));

    if ((s_state != FW_ST_RECEIVING) || (s_written_high < s_info.fw_size) ||
        (end.fw_size != s_info.fw_size))
    {
        send_ack(FW_CMD_END, FW_STATUS_SIZE_ERR, 0U);
        return;
    }

    s_state = FW_ST_VERIFY;
    up_log("[UP] verify...\r\n");

    crc = w25q_crc32(W25Q64_FW_BASE, s_info.fw_size);
    if ((crc != s_info.fw_crc32) || (crc != end.fw_crc32))
    {
        up_log("[UP] crc error\r\n");
        s_state = FW_ST_ERROR;
        send_ack(FW_CMD_END, FW_STATUS_CRC_ERR, 0U);
        return;
    }

    if (fw_param_load(&param) != 0)
    {
        fw_param_default(&param);
    }
    param.update_req = 1U;
    param.fw_size    = s_info.fw_size;
    param.fw_crc32   = s_info.fw_crc32;
    param.fw_version = s_info.fw_version;
    param.boot_try   = 0U;

    if (fw_param_save(&param) != 0)
    {
        up_log("[UP] save flag failed\r\n");
        s_state = FW_ST_ERROR;
        send_ack(FW_CMD_END, FW_STATUS_WRITE_ERR, 0U);
        return;
    }

    s_state = FW_ST_DONE;
    up_log("[UP] done, rebooting to bootloader\r\n");
    send_ack(FW_CMD_END, FW_STATUS_OK, 0U);
    HAL_Delay(200U);
    NVIC_SystemReset();
}

/* --------------------------------- 入口 --------------------------------- */
void fw_update_feed(const uint8_t *data, uint16_t len)
{
    uint16_t   i;
    fw_frame_t frame;

    for (i = 0U; i < len; i++)
    {
        if (fw_parser_push(&s_parser, data[i], &frame) == FW_PARSE_OK)
        {
            switch (frame.cmd)
            {
                case FW_CMD_START: handle_start(&frame); break;
                case FW_CMD_DATA:  handle_data(&frame);  break;
                case FW_CMD_END:   handle_end(&frame);   break;
                default:           break;
            }
        }
    }
}
