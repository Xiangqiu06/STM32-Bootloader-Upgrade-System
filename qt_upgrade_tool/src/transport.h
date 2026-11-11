#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <QByteArray>
#include <QObject>
#include <QString>

/* 链路抽象：三种链路对上提供同样的"收发字节流"接口，
 * 升级状态机完全复用，这就是"多链路升级"在工具侧的体现。
 */
class Transport : public QObject
{
    Q_OBJECT
public:
    explicit Transport(QObject *parent = nullptr) : QObject(parent) {}
    ~Transport() override = default;

    virtual bool open(QString *err) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual bool send(const QByteArray &data) = 0;
    virtual QString description() const = 0;

signals:
    void bytesReceived(const QByteArray &data);
    void errorOccurred(const QString &msg);
};

#endif // TRANSPORT_H
