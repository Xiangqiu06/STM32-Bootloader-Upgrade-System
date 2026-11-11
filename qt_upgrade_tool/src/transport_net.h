#ifndef TRANSPORT_NET_H
#define TRANSPORT_NET_H

#include "transport.h"

/* 网络升级占位实现（预留）。
 * 后续接 WiFi 模块时，在这里用 QTcpSocket / QMqttClient 实现：
 *   - TCP：把 buildFrame() 产生的字节流直接 write 出去
 *   - MQTT：发布到 topic，payload 用同一份字节流
 * 升级状态机与 UART 完全一致，不需要改动 UpgradeSession。
 */
class NetTransport : public Transport
{
    Q_OBJECT
public:
    explicit NetTransport(QObject *parent = nullptr) : Transport(parent) {}

    void setEndpoint(const QString &host, quint16 port)
    {
        m_host = host;
        m_port = port;
    }

    bool open(QString *err) override
    {
        if (err) {
            *err = QStringLiteral("网络升级为预留接口（暂未实现，需 WiFi 模块）。"
                                  "主机 %1:%2").arg(m_host).arg(m_port);
        }
        return false;
    }
    void close() override {}
    bool isOpen() const override { return false; }
    bool send(const QByteArray &) override { return false; }
    QString description() const override
    {
        return QStringLiteral("NET %1:%2 (预留)").arg(m_host).arg(m_port);
    }

private:
    QString m_host = QStringLiteral("192.168.4.1");
    quint16 m_port = 8266;
};

#endif // TRANSPORT_NET_H
