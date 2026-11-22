#ifndef TRANSPORT_CAN_H
#define TRANSPORT_CAN_H

#include "transport.h"

class QSocketNotifier;

/* CAN 链路：Linux SocketCAN 原生套接字，无需第三方库。
 * 升级数据按 8 字节一帧拆到 CAN 帧，接收侧按序拼回字节流。
 * 帧 ID：主机 -> 设备 0x123；设备 -> 主机 0x124（可在界面改）
 */
class CanTransport : public Transport
{
    Q_OBJECT
public:
    explicit CanTransport(QObject *parent = nullptr);
    ~CanTransport() override;

    void setInterface(const QString &iface) { m_iface = iface; }
    void setTxId(quint32 id) { m_txId = id; }
    void setRxId(quint32 id) { m_rxId = id; }

    bool open(QString *err) override;
    void close() override;
    bool isOpen() const override { return m_open; }
    bool send(const QByteArray &data) override;
    QString description() const override;

private slots:
    void onReadable();

private:
    QString          m_iface = QStringLiteral("can0");
    quint32          m_txId  = 0x123;
    quint32          m_rxId  = 0x124;
    int              m_sock  = -1;
    bool             m_open  = false;
    QSocketNotifier *m_notifier = nullptr;
};

#endif // TRANSPORT_CAN_H
