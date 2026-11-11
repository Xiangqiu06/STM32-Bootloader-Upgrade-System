#include "transport_can.h"

#include <QSocketNotifier>
#include <QtGlobal>

#ifdef Q_OS_LINUX
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#endif

CanTransport::CanTransport(QObject *parent)
    : Transport(parent)
{
}

CanTransport::~CanTransport()
{
    close();
}

bool CanTransport::open(QString *err)
{
#ifdef Q_OS_LINUX
    close();

    m_sock = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_sock < 0) {
        if (err) {
            *err = QStringLiteral("创建 CAN socket 失败（需要 root 或已加载 vcan/can 驱动）");
        }
        return false;
    }

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    const QByteArray name = m_iface.toLocal8Bit();
    std::strncpy(ifr.ifr_name, name.constData(), IFNAMSIZ - 1);
    if (::ioctl(m_sock, SIOCGIFINDEX, &ifr) < 0) {
        if (err) {
            *err = QStringLiteral("接口 %1 不存在，请先执行：sudo ip link set %1 up type can bitrate 500000")
                       .arg(m_iface);
        }
        ::close(m_sock);
        m_sock = -1;
        return false;
    }

    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(m_sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        if (err) {
            *err = QStringLiteral("绑定 %1 失败（接口是否已 up？）").arg(m_iface);
        }
        ::close(m_sock);
        m_sock = -1;
        return false;
    }

    /* 只接收设备回传的 ID，避免总线上其他报文干扰 */
    struct can_filter filter;
    filter.can_id   = m_rxId;
    filter.can_mask = CAN_SFF_MASK;
    ::setsockopt(m_sock, SOL_CAN_RAW, CAN_RAW_FILTER, &filter, sizeof(filter));

    m_notifier = new QSocketNotifier(m_sock, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &CanTransport::onReadable);

    m_open = true;
    return true;
#else
    if (err) {
        *err = QStringLiteral("CAN 升级仅支持 Linux SocketCAN");
    }
    return false;
#endif
}

void CanTransport::close()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }
#ifdef Q_OS_LINUX
    if (m_sock >= 0) {
        ::close(m_sock);
        m_sock = -1;
    }
#endif
    m_open = false;
}

bool CanTransport::send(const QByteArray &data)
{
#ifdef Q_OS_LINUX
    if (!m_open || m_sock < 0) {
        return false;
    }
    int offset = 0;
    while (offset < data.size()) {
        struct can_frame frame;
        std::memset(&frame, 0, sizeof(frame));
        frame.can_id  = m_txId;
        const int n   = qMin(8, data.size() - offset);
        frame.can_dlc = static_cast<__u8>(n);
        std::memcpy(frame.data, data.constData() + offset, static_cast<size_t>(n));

        if (::write(m_sock, &frame, sizeof(frame)) != static_cast<ssize_t>(sizeof(frame))) {
            return false;
        }
        offset += n;
    }
    return true;
#else
    Q_UNUSED(data)
    return false;
#endif
}

QString CanTransport::description() const
{
    return QStringLiteral("%1  TX=0x%2  RX=0x%3")
        .arg(m_iface)
        .arg(m_txId, 3, 16, QChar('0'))
        .arg(m_rxId, 3, 16, QChar('0'));
}

void CanTransport::onReadable()
{
#ifdef Q_OS_LINUX
    if (!m_open || m_sock < 0) {
        return;
    }
    for (;;) {
        struct can_frame frame;
        const ssize_t n = ::read(m_sock, &frame, sizeof(frame));
        if (n < static_cast<ssize_t>(sizeof(frame))) {
            break;
        }
        if (frame.can_id != m_rxId || frame.can_dlc == 0) {
            continue;
        }
        emit bytesReceived(QByteArray(reinterpret_cast<const char *>(frame.data), frame.can_dlc));
    }
#endif
}
