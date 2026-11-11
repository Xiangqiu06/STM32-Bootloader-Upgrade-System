#ifndef HEXLOADER_H
#define HEXLOADER_H

#include <QByteArray>
#include <QString>

/* Intel HEX 文件解析（Keil 默认输出的 .hex）
 *
 * 支持记录类型 00(数据) / 01(EOF) / 04(扩展线性地址)。
 * 解析结果按线性地址偏移平铺为纯二进制。
 * 常见情况（单个连续段）返回基址 0；含多段时用 firstAddr 告知
 * 第一段的线性地址，便于调用方提示用户核对目标分区。
 */
namespace hexloader {

bool parseIntelHex(const QString &path,
                   QByteArray &out,
                   quint32    &firstAddr,
                   QString    &err);

} // namespace hexloader

#endif // HEXLOADER_H
