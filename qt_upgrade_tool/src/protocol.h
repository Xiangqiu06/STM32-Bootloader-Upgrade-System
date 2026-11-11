#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>
#include <functional>

/* 协议与固件 common/fw_proto.h 保持一致：
 * 帧格式  AA 55 | CMD | LEN(2, 小端) | PAYLOAD | CRC16(2, 小端)
 * CRC16 覆盖 CMD+LEN+PAYLOAD，初值 0xFFFF，多项式 0xA001（Modbus）
 */
namespace fw {

constexpr quint8 HEAD0 = 0xAA;
constexpr quint8 HEAD1 = 0x55;
constexpr int    MAX_PAYLOAD = 1100;

enum Command : quint8 {
    CMD_START = 0x01,
    CMD_DATA  = 0x02,
    CMD_END   = 0x03,
    CMD_ACK   = 0x81
};

enum Status : quint8 {
    ST_OK    = 0,   /* 成功 */
    ST_PARAM = 1,   /* 参数错误（含目标分区不支持） */
    ST_ERASE = 2,   /* 擦除失败 */
    ST_WRITE = 3,   /* 写入失败 */
    ST_CRC   = 4,   /* CRC 校验失败 */
    ST_STATE = 5,   /* 状态错误 */
    ST_SIZE  = 6    /* 长度/大小错误 */
};

quint16 crc16_modbus(const QByteArray &data);
quint32 crc32_zlib(const QByteArray &data);

QByteArray buildFrame(quint8 cmd, const QByteArray &payload);

QString statusText(int status);

/* 流式解析：支持半包、粘包、丢字节自愈 */
class FrameParser
{
public:
    using Callback = std::function<void(quint8 cmd, const QByteArray &payload)>;

    void reset() { m_buf.clear(); }
    void feed(const QByteArray &data, const Callback &cb);

private:
    QByteArray m_buf;
};

} // namespace fw

#endif // PROTOCOL_H
