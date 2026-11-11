#include "upgradesession.h"
#include "transport.h"

#include <QDataStream>
#include <QIODevice>

UpgradeSession::UpgradeSession(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &UpgradeSession::onTimeout);
}

bool UpgradeSession::start(const QByteArray &image, Target target, quint16 version,
                          int chunk, int retries, QString *err)
{
    if (!m_transport || !m_transport->isOpen()) {
        if (err) *err = QStringLiteral("链路未打开");
        return false;
    }
    if (image.isEmpty()) {
        if (err) *err = QStringLiteral("固件为空");
        return false;
    }
    if (chunk < 32 || chunk > fw::MAX_PAYLOAD) {
        if (err) *err = QStringLiteral("分片长度必须在 32 ~ %1 之间").arg(fw::MAX_PAYLOAD);
        return false;
    }

    m_image      = image;
    m_crc32      = fw::crc32_zlib(image);
    m_version    = version;
    m_target     = static_cast<quint8>(target);
    m_chunk      = chunk;
    m_retriesMax = qMax(1, retries);
    m_retry      = 0;
    m_offset     = 0;
    m_seq        = 0;
    m_parser.reset();

    connect(m_transport, &Transport::bytesReceived,
            this, &UpgradeSession::onBytes, Qt::UniqueConnection);

    emit log(QStringLiteral("固件 %1 字节，CRC32=0x%2，版本 %3.%4，目标 %5")
                 .arg(m_image.size())
                 .arg(m_crc32, 8, 16, QChar('0'))
                 .arg((m_version >> 8) & 0xFF)
                 .arg(m_version & 0xFF)
                 .arg(m_target == 0 ? QStringLiteral("APP")
                      : (m_target == 1 ? QStringLiteral("RESET") : QStringLiteral("BOOT"))));

    m_state = State::WaitStartAck;
    sendStart();
    return true;
}

void UpgradeSession::sendStart()
{
    /* 设备每次收到 START 都会重新擦除暂存区、从头等待 seq=0，
     * 因此任何一次（重新）握手都必须从第 0 包重新传。 */
    m_seq    = 0;
    m_offset = 0;

    /* 兼容策略：
     *   APP   -> 12 字节 START（与现有固件一致，可直接使用）
     *   RESET -> 13 字节 START，末字节为目标号（需固件 v1.1+）
     *   BOOT  -> 同上
     * 目标号放在末尾，旧固件会因长度不符返回"参数错误"，不会误写 Flash。
     */
    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint32>(m_image.size())
       << m_crc32
       << m_version
       << static_cast<quint16>(m_chunk);
    if (m_target != 0) {
        payload.append(static_cast<char>(m_target));
    }

    emit log(QStringLiteral("→ START（%1 字节，分片 %2）").arg(payload.size()).arg(m_chunk));

    const QByteArray frame = fw::buildFrame(fw::CMD_START, payload);
    {
        QString hex;
        const int n = qMin<int>(frame.size(), 24);
        for (int i = 0; i < n; ++i) {
            hex += QString::number(static_cast<quint8>(frame.at(i)), 16).rightJustified(2, QLatin1Char('0'))
                   + QLatin1Char(' ');
        }
        emit log(QStringLiteral("  帧内容: %1%2").arg(hex, frame.size() > 24 ? QStringLiteral("…") : QString()));
    }
    m_transport->send(frame);
    m_ackWaitCmd = fw::CMD_START;
    armTimer(10000);           /* 设备要先擦除暂存区，给足时间 */
}

void UpgradeSession::sendNextChunk()
{
    if (m_offset >= m_image.size()) {
        sendEnd();
        return;
    }

    const int n = qMin(m_chunk, m_image.size() - m_offset);
    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << m_seq << static_cast<quint32>(m_offset) << static_cast<quint16>(n);
    payload.append(m_image.constData() + m_offset, n);

    if (!m_transport->send(fw::buildFrame(fw::CMD_DATA, payload))) {
        fail(QStringLiteral("发送失败，链路可能已断开"));
        return;
    }

    /* 只记录第一个分片的帧内容，便于核对线上格式 */
    if (m_seq == 0) {
        const QByteArray frame = fw::buildFrame(fw::CMD_DATA, payload);
        QString hex;
        const int n = qMin<int>(frame.size(), 40);
        for (int i = 0; i < n; ++i) {
            hex += QString::number(static_cast<quint8>(frame.at(i)), 16).rightJustified(2, QLatin1Char('0'))
                   + QLatin1Char(' ');
        }
        emit log(QStringLiteral("  首个 DATA 帧头: %1…（共 %2 字节）")
                     .arg(hex, QString::number(frame.size())));
    }
    emit log(QStringLiteral("→ DATA seq=%1 offset=%2 len=%3")
                 .arg(m_seq).arg(m_offset).arg(n));

    m_state = State::WaitDataAck;
    m_ackWaitCmd = fw::CMD_DATA;
    armTimer(2000);
}

void UpgradeSession::sendEnd()
{
    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint32>(m_image.size()) << m_crc32;

    emit log(QStringLiteral("→ END，请求设备校验并重启"));
    m_transport->send(fw::buildFrame(fw::CMD_END, payload));
    m_state = State::WaitEndAck;
    m_ackWaitCmd = fw::CMD_END;
    armTimer(15000);
}

void UpgradeSession::armTimer(int ms)
{
    m_timer.start(ms);
}

void UpgradeSession::onBytes(const QByteArray &data)
{
    /* END 阶段兜底：设备整包校验通过后会立刻置标志并复位（Bootloader
     * 接管搬运），串口上会先出现 "[UP] done"、随后是 "[BOOT]" 日志。
     * END 的 OK ACK 偶尔会在设备复位瞬间丢失（实测发生过），只要看到
     * 这些日志就说明校验已通过、迁移已开始——判定升级成功，不再重发
     * END（重发只会打到重启后的新固件上，得到无意义的 SIZE_ERR）。
     * 校验失败时设备只回 CRC_ERR 不会复位，所以这个判据是安全的。 */
    if (m_state == State::WaitEndAck) {
        if (data.contains("[UP] done") || data.contains("[BOOT]")) {
            emit log(QStringLiteral("检测到设备校验完成并已重启，Bootloader 正在搬运固件"));
            succeed();
            return;
        }
    }

    /* 设备日志是 ASCII 文本，会被解析器自动跳过 */
    m_parser.feed(data, [this](quint8 cmd, const QByteArray &payload) {
        if (cmd != fw::CMD_ACK || payload.size() < 4) {
            return;
        }
        const quint8  ackCmd = static_cast<quint8>(payload.at(0));
        const quint8  status = static_cast<quint8>(payload.at(1));
        const quint16 seq    = static_cast<quint8>(payload.at(2))
                             | (static_cast<quint16>(static_cast<quint8>(payload.at(3))) << 8);
        handleAck(ackCmd, status, seq);
    });
}

void UpgradeSession::handleAck(quint8 ackCmd, quint8 status, quint16 seq)
{
    if (m_state == State::Idle) {
        return;
    }
    if (ackCmd != m_ackWaitCmd) {
        return;                       /* 迟到的上一包 ACK，忽略 */
    }
    if (ackCmd == fw::CMD_DATA && seq != m_seq) {
        return;
    }

    m_timer.stop();

    if (status != fw::ST_OK) {
        QString extra;
        if (status == fw::ST_PARAM && m_target != 0) {
            extra = QStringLiteral("（设备固件可能还不支持 RESET/BOOT 目标区，需固件 v1.1+）");
        }
        fail(QStringLiteral("设备返回：%1%2").arg(fw::statusText(status), extra));
        return;
    }

    switch (m_state) {
    case State::WaitStartAck:
        emit log(QStringLiteral("设备已擦除暂存区，开始传输"));
        m_retry = 0;
        m_state = State::SendData;
        sendNextChunk();
        break;

    case State::WaitDataAck:
        m_offset += qMin(m_chunk, m_image.size() - m_offset);
        m_seq = static_cast<quint16>((m_seq + 1) & 0xFFFF);
        m_retry = 0;
        emit progress(m_offset, m_image.size());
        sendNextChunk();
        break;

    case State::WaitEndAck:
        succeed();
        break;

    default:
        break;
    }
}

void UpgradeSession::onTimeout()
{
    if (m_state == State::Idle) {
        return;
    }
    if (m_retry < m_retriesMax) {
        ++m_retry;
        emit log(QStringLiteral("ACK 超时，第 %1/%2 次重发").arg(m_retry).arg(m_retriesMax));

        switch (m_state) {
        case State::WaitStartAck:
            /* 握手失败就重新握手（设备可能正在擦除，给足耐心） */
            sendStart();
            break;

        case State::WaitDataAck:
            /* 先原地重发当前包（设备按 offset 写 Flash，重复写同一包无害）。
             * 连续 2 次都失败，大概率是设备解析器停在半帧状态等 payload，
             * 原地重发会被吞进上一帧——这时再重新走 START 握手彻底复位。 */
            if (m_retry <= 2) {
                sendNextChunk();
            } else {
                m_retry = 0;
                m_state = State::WaitStartAck;
                sendStart();
            }
            break;

        case State::WaitEndAck:
            sendEnd();
            break;

        default:
            break;
        }
        return;
    }
    fail(QStringLiteral("等待设备响应超时（已重试 %1 次）").arg(m_retriesMax));
}

void UpgradeSession::fail(const QString &why)
{
    m_timer.stop();
    m_state = State::Idle;
    emit log(QStringLiteral("✗ %1").arg(why));
    emit finished(false, why);
}

void UpgradeSession::succeed()
{
    m_timer.stop();
    m_state = State::Idle;
    emit progress(m_image.size(), m_image.size());
    emit log(QStringLiteral("✓ 传输完成，设备正在重启并搬运固件"));
    emit finished(true, QStringLiteral("升级成功，等待设备重启"));
}

void UpgradeSession::abort(const QString &reason)
{
    if (m_state == State::Idle) {
        return;
    }
    m_timer.stop();
    m_state = State::Idle;
    emit log(QStringLiteral("■ 已中止：%1").arg(reason));
    emit finished(false, reason);
}
