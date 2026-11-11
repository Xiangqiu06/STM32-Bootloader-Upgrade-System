#include "hexloader.h"

#include <QFile>
#include <QTextStream>
#include <QVector>
#include <algorithm>

namespace hexloader {

namespace {

struct Span {
    quint32    addr;
    QByteArray data;
};

struct HexVal {
    int v;
    bool ok;
};

inline HexVal hexVal(QChar c)
{
    if (c >= QLatin1Char('0') && c <= QLatin1Char('9')) return { c.unicode() - QLatin1Char('0').unicode(), true };
    if (c >= QLatin1Char('a') && c <= QLatin1Char('f')) return { c.unicode() - QLatin1Char('a').unicode() + 10, true };
    if (c >= QLatin1Char('A') && c <= QLatin1Char('F')) return { c.unicode() - QLatin1Char('A').unicode() + 10, true };
    return { -1, false };
}

/*
 * Intel HEX 记录布局（逐字符）：
 *   :  LL  AAAA  TT  DD DD ...  CC
 *   0  1-2 3-6   7-8 9...       末2位
 * 统一先把整行转成字节数组再按下标取，避免逐半字节索引出错。
 * 返回后 type/addr 为记录的类型与 16 位地址，data 为数据字段。
 */
bool parseLine(const QString &line, quint8 &type, quint32 &addr, QByteArray &data, QString &err)
{
    const int hexLen = static_cast<int>(line.size());
    if (hexLen < 11 || line.at(0) != QLatin1Char(':')) {
        err = QStringLiteral("记录格式非法");
        return false;
    }

    /* 整行（除冒号）转字节，同时累加校验和 */
    QByteArray bytes;
    bytes.reserve((hexLen - 1) / 2);
    int sum = 0;
    for (int i = 1; i + 1 < hexLen; i += 2) {
        const HexVal hv1 = hexVal(line.at(i));
        const HexVal hv2 = hexVal(line.at(i + 1));
        if (!hv1.ok || !hv2.ok) {
            err = QStringLiteral("记录含非法字符");
            return false;
        }
        const int b = (hv1.v << 4) | hv2.v;
        sum += b;
        bytes.append(static_cast<char>(b));
    }
    if ((sum & 0xFF) != 0) {
        err = QStringLiteral("校验和不匹配");
        return false;
    }

    if (bytes.size() < 5) {
        err = QStringLiteral("记录过短");
        return false;
    }

    const quint8  count   = static_cast<quint8>(bytes.at(0));
    const quint16 recAddr = static_cast<quint16>((static_cast<quint8>(bytes.at(1)) << 8)
                                                 | static_cast<quint8>(bytes.at(2)));
    type = static_cast<quint8>(bytes.at(3));

    if (bytes.size() != count + 5) {
        err = QStringLiteral("长度字段与实际不符");
        return false;
    }

    data = bytes.mid(4, count);
    addr = recAddr;
    return true;
}

} // namespace

bool parseIntelHex(const QString &path, QByteArray &out, quint32 &firstAddr, QString &err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        err = QStringLiteral("无法打开文件");
        return false;
    }

    QTextStream ts(&f);
    QVector<Span> spans;
    quint32 base     = 0;
    quint32 minAddr  = 0xFFFFFFFFu;
    quint32 maxEnd   = 0;
    bool    haveData = false;
    bool    sawEof   = false;

    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        quint8     type = 0;
        quint32    addr = 0;
        QByteArray data;
        if (!parseLine(line, type, addr, data, err)) {
            err = QStringLiteral("%1\n行：%2").arg(err, line.left(48));
            return false;
        }

        switch (type) {
        case 0x00: { /* 数据 */
            const quint32 linear = base + addr;
            if (!haveData) {
                firstAddr = linear;
            }
            minAddr = std::min(minAddr, linear);
            maxEnd  = std::max(maxEnd, linear + static_cast<quint32>(data.size()));
            Span sp;
            sp.addr = linear;
            sp.data = data;
            spans.append(sp);
            haveData = true;
            break;
        }
        case 0x01: /* EOF */
            sawEof = true;
            break;
        case 0x02: /* 扩展段地址（80x86 寻址，MCU 固件不会用，拒绝避免静默错位） */
            err = QStringLiteral("不支持的记录类型 02（扩展段地址）");
            return false;
        case 0x04: /* 扩展线性地址：高 16 位在数据字段 */
            if (data.size() != 2) {
                err = QStringLiteral("类型 04 记录长度异常");
                return false;
            }
            base = (static_cast<quint32>(static_cast<quint8>(data.at(0))) << 8
                    | static_cast<quint32>(static_cast<quint8>(data.at(1)))) << 16;
            break;
        default:   /* 03/05 起始地址记录，忽略 */
            break;
        }
    }
    f.close();

    if (!haveData) {
        err = QStringLiteral("文件中没有数据记录");
        return false;
    }
    if (!sawEof) {
        err = QStringLiteral("缺少 EOF 记录，文件可能不完整");
        return false;
    }

    /* 按 0xFF 平铺（Flash 擦除态即 0xFF，未写区域语义正确） */
    const quint32 total = maxEnd - minAddr;
    if (total > 512u * 1024u) {
        err = QStringLiteral("镜像跨度过大（%1 字节），疑似地址异常").arg(total);
        return false;
    }
    out = QByteArray(static_cast<int>(total), static_cast<char>(0xFF));
    for (const Span &s : spans) {
        const int off = static_cast<int>(s.addr - minAddr);
        out.replace(off, s.data.size(), s.data);
    }

    if (minAddr != firstAddr) {
        firstAddr = minAddr;    /* 以实际最小地址为准 */
    }
    return true;
}

} // namespace hexloader
