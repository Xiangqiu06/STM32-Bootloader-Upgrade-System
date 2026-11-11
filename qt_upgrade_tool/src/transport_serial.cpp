#include "transport_serial.h"

SerialTransport::SerialTransport(QObject *parent)
    : Transport(parent)
{
    connect(&m_port, &QSerialPort::readyRead, this, &SerialTransport::onReadyRead);
    connect(&m_port, static_cast<void (QSerialPort::*)(QSerialPort::SerialPortError)>(&QSerialPort::error),
            this, &SerialTransport::onError);
}

bool SerialTransport::open(QString *err)
{
    if (m_port.isOpen()) {
        m_port.close();
    }
    m_port.setPortName(m_portName);
    m_port.setBaudRate(m_baud);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port.open(QIODevice::ReadWrite)) {
        if (err) {
            *err = QStringLiteral("打开 %1 失败：%2").arg(m_portName, m_port.errorString());
        }
        return false;
    }

    /* 野火等开发板板载 CH340 带一键下载电路（DTR/RTS 控制 NRST/BOOT0），
     * 默认电平可能把板子按在复位或送进 ISP 模式，这里必须置为无效。
     */
    m_port.setDataTerminalReady(false);
    m_port.setRequestToSend(false);
    m_port.clear(QSerialPort::AllDirections);
    return true;
}

void SerialTransport::close()
{
    if (m_port.isOpen()) {
        m_ignoreError = true;
        m_port.close();
        m_ignoreError = false;
    }
}

bool SerialTransport::send(const QByteArray &data)
{
    if (!m_port.isOpen()) {
        return false;
    }
    const qint64 n = m_port.write(data);
    /* 大帧必须确保真正发出（CH340/驱动可能有缓冲），否则设备收不全帧 */
    m_port.flush();
    m_port.waitForBytesWritten(200);
    return n == data.size();
}

QString SerialTransport::description() const
{
    return QStringLiteral("%1 @ %2 8N1").arg(m_portName).arg(m_baud);
}

void SerialTransport::onReadyRead()
{
    const QByteArray data = m_port.readAll();
    if (!data.isEmpty()) {
        emit bytesReceived(data);
    }
}

void SerialTransport::onError(QSerialPort::SerialPortError err)
{
    if (m_ignoreError || err == QSerialPort::NoError) {
        return;
    }
    if (err == QSerialPort::ResourceError || err == QSerialPort::DeviceNotFoundError) {
        emit errorOccurred(QStringLiteral("串口异常：%1").arg(m_port.errorString()));
    }
}
