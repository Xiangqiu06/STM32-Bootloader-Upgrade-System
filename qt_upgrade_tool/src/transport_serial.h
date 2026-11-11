#ifndef TRANSPORT_SERIAL_H
#define TRANSPORT_SERIAL_H

#include "transport.h"

#include <QSerialPort>

/* UART 与 LoRa 共用：LoRa 模块对外就是串口透传，
 * 区别只在默认波特率、分片大小和包间延时（由界面上层决定）。
 */
class SerialTransport : public Transport
{
    Q_OBJECT
public:
    explicit SerialTransport(QObject *parent = nullptr);

    void setPortName(const QString &name) { m_portName = name; }
    void setBaudRate(int baud) { m_baud = baud; }

    bool open(QString *err) override;
    void close() override;
    bool isOpen() const override { return m_port.isOpen(); }
    bool send(const QByteArray &data) override;
    QString description() const override;

private slots:
    void onReadyRead();
    void onError(QSerialPort::SerialPortError err);

private:
    QSerialPort m_port;
    QString     m_portName;
    int         m_baud = 115200;
    bool        m_ignoreError = false;
};

#endif // TRANSPORT_SERIAL_H
