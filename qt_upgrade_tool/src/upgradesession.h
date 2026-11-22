#ifndef UPGRADESESSION_H
#define UPGRADESESSION_H

#include "protocol.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

class Transport;

/* 目标分区：与固件侧的 target 字节对应 */
enum class Target : quint8 {
    App   = 0,
    Reset = 1,
    Boot  = 2
};

class UpgradeSession : public QObject
{
    Q_OBJECT
public:
    explicit UpgradeSession(QObject *parent = nullptr);

    void setTransport(Transport *t) { m_transport = t; }

    /* 启动一次升级；返回 false 表示参数非法（此时不会发出任何数据） */
    bool start(const QByteArray &image, Target target, quint16 version,
               int chunk, int retries, QString *err);

    void abort(const QString &reason);
    bool isRunning() const { return m_state != State::Idle; }

signals:
    void log(const QString &line);
    void progress(qint64 sent, qint64 total);
    void finished(bool ok, const QString &message);

private slots:
    void onBytes(const QByteArray &data);
    void onTimeout();

private:
    enum class State {
        Idle,
        WaitStartAck,
        SendData,
        WaitDataAck,
        WaitEndAck
    };

    void sendStart();
    void sendNextChunk();
    void sendEnd();
    void armTimer(int ms);
    void fail(const QString &why);
    void succeed();
    void handleAck(quint8 ackCmd, quint8 status, quint16 seq);

    Transport   *m_transport = nullptr;
    fw::FrameParser m_parser;
    QTimer       m_timer;

    State        m_state = State::Idle;
    QByteArray   m_image;
    quint32      m_crc32 = 0;
    quint16      m_version = 0;
    quint8       m_target = 0;
    int          m_chunk = 1024;
    int          m_retriesMax = 5;
    int          m_retry = 0;
    int          m_offset = 0;
    quint16      m_seq = 0;
    int          m_ackWaitCmd = 0;
};

#endif // UPGRADESESSION_H
