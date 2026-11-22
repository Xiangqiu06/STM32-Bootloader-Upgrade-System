#include <string.h>
#include "fw_proto.h"
#include "crc_utils.h"

uint16_t fw_frame_build(uint8_t cmd, const uint8_t *payload, uint16_t plen,
                        uint8_t *out, uint16_t out_cap)
{
    uint16_t total = (uint16_t)(plen + FW_FRAME_OVERHEAD);
    uint16_t crc;

    if ((out == 0) || (total > out_cap) || (plen > FW_PAYLOAD_MAX))
    {
        return 0U;
    }

    out[0] = FW_FRAME_HEAD0;
    out[1] = FW_FRAME_HEAD1;
    out[2] = cmd;
    out[3] = (uint8_t)(plen & 0xFFU);
    out[4] = (uint8_t)((plen >> 8) & 0xFFU);
    if ((plen > 0U) && (payload != 0))
    {
        memcpy(&out[5], payload, plen);
    }

    crc = crc16_modbus(&out[2], (uint32_t)plen + 3U);
    out[5U + plen] = (uint8_t)(crc & 0xFFU);
    out[6U + plen] = (uint8_t)((crc >> 8) & 0xFFU);

    return total;
}

void fw_parser_init(fw_parser_t *p)
{
    p->state = 0U;
    p->idx   = 0U;
    p->plen  = 0U;
}

int fw_parser_push(fw_parser_t *p, uint8_t byte, fw_frame_t *out)
{
    switch (p->state)
    {
        case 0U:    /* 等帧头1 */
            if (byte == FW_FRAME_HEAD0)
            {
                p->buf[0] = byte;
                p->idx    = 1U;
                p->state  = 1U;
            }
            break;

        case 1U:    /* 等帧头2 */
            if (byte == FW_FRAME_HEAD1)
            {
                p->buf[1] = byte;
                p->idx    = 2U;
                p->state  = 2U;
            }
            else if (byte != FW_FRAME_HEAD0)
            {
                p->state = 0U;
            }
            break;

        case 2U:    /* CMD */
            p->buf[p->idx++] = byte;
            p->state = 3U;
            break;

        case 3U:    /* LEN 低字节 */
            p->buf[p->idx++] = byte;
            p->state = 4U;
            break;

        case 4U:    /* LEN 高字节 -> 校验长度 */
            p->buf[p->idx++] = byte;
            p->plen = (uint16_t)((uint16_t)p->buf[3] | ((uint16_t)p->buf[4] << 8));
            if (p->plen > FW_PAYLOAD_MAX)
            {
                fw_parser_init(p);
                return FW_PARSE_BAD_LEN;
            }
            p->state = (p->plen == 0U) ? 6U : 5U;
            break;

        case 5U:    /* PAYLOAD */
            p->buf[p->idx++] = byte;
            if (p->idx == (uint16_t)(5U + p->plen))
            {
                p->state = 6U;
            }
            break;

        case 6U:    /* CRC 低字节 */
            p->buf[p->idx++] = byte;
            p->state = 7U;
            break;

        case 7U:    /* CRC 高字节 -> 校验 */
        {
            uint16_t crc_rx;
            uint16_t crc_calc;

            p->buf[p->idx++] = byte;
            crc_rx   = (uint16_t)((uint16_t)p->buf[p->idx - 2U] |
                                  ((uint16_t)p->buf[p->idx - 1U] << 8));
            crc_calc = crc16_modbus(&p->buf[2], (uint32_t)p->idx - 4U);

            p->state = 0U;
            p->idx   = 0U;

            if (crc_rx != crc_calc)
            {
                return FW_PARSE_BAD_CRC;
            }

            out->cmd     = p->buf[2];
            out->len     = p->plen;
            out->payload = &p->buf[5];  /* buf: [0]AA [1]55 [2]CMD [3..4]LEN [5..]payload */
            return FW_PARSE_OK;
        }

        default:
            p->state = 0U;
            p->idx   = 0U;
            break;
    }

    return FW_PARSE_MORE;
}
