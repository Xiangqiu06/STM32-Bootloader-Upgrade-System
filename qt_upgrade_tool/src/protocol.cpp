#include "protocol.h"

namespace fw {

quint16 crc16_modbus(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= static_cast<quint8>(data.at(i));
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x0001) {
                crc = static_cast<quint16>((crc >> 1) ^ 0xA001);
            } else {
                crc = static_cast<quint16>(crc >> 1);
            }
        }
    }
    return crc;
}

quint32 crc32_zlib(const QByteArray &data)
{
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        init = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    for (int i = 0; i < data.size(); ++i) {
        crc = table[(crc ^ static_cast<quint8>(data.at(i))) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

QByteArray buildFrame(quint8 cmd, const QByteArray &payload)
{
    QByteArray body;
    body.reserve(payload.size() + 3);
    body.append(static_cast<char>(cmd));
    const quint16 len = static_cast<quint16>(payload.size());
    body.append(static_cast<char>(len & 0xFF));
    body.append(static_cast<char>((len >> 8) & 0xFF));
    body.append(payload);

    const quint16 crc = crc16_modbus(body);
    QByteArray out;
    out.reserve(body.size() + 4);
    out.append(static_cast<char>(HEAD0));
    out.append(static_cast<char>(HEAD1));
    out.append(body);
    out.append(static_cast<char>(crc & 0xFF));
    out.append(static_cast<char>((crc >> 8) & 0xFF));
    return out;
}

QString statusText(int status)
{
    switch (status) {
    case ST_OK:    return QStringLiteral("成功");
    case ST_PARAM: return QStringLiteral("参数错误");
    case ST_ERASE: return QStringLiteral("擦除失败");
    case ST_WRITE: return QStringLiteral("写入失败");
    case ST_CRC:   return QStringLiteral("CRC 校验失败");
    case ST_STATE: return QStringLiteral("状态错误");
    case ST_SIZE:  return QStringLiteral("长度/大小错误");
    default:       return QStringLiteral("未知状态(%1)").arg(status);
    }
}

void FrameParser::feed(const QByteArray &data, const Callback &cb)
{
    m_buf.append(data);

    for (;;) {
        const int idx = m_buf.indexOf(QByteArray::fromHex("AA55"));
        if (idx < 0) {
            if (m_buf.size() > 1) {
                m_buf = m_buf.right(1);
            }
            return;
        }
        if (idx > 0) {
            m_buf.remove(0, idx);
        }
        if (m_buf.size() < 5) {
            return;
        }

        const quint16 len = static_cast<quint8>(m_buf.at(3))
                          | (static_cast<quint16>(static_cast<quint8>(m_buf.at(4))) << 8);
        if (len > MAX_PAYLOAD) {
            m_buf.remove(0, 2);          /* 长度异常，丢掉帧头重新同步 */
            continue;
        }

        const int total = 5 + len + 2;
        if (m_buf.size() < total) {
            return;                      /* 半包，等后续数据 */
        }

        const QByteArray body = m_buf.mid(2, 3 + len);
        const quint16 crcRx = static_cast<quint8>(m_buf.at(total - 2))
                            | (static_cast<quint16>(static_cast<quint8>(m_buf.at(total - 1))) << 8);
        if (crcRx != crc16_modbus(body)) {
            m_buf.remove(0, 2);          /* CRC 错，重新找帧头 */
            continue;
        }

        const quint8 cmd = static_cast<quint8>(m_buf.at(2));
        const QByteArray payload = m_buf.mid(5, len);
        m_buf.remove(0, total);
        cb(cmd, payload);
    }
}

} // namespace fw
