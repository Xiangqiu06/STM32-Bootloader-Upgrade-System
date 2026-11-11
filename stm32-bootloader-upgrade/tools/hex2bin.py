#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Intel HEX 转 BIN（用于给 fw_send.py 发送升级固件）

用法:
    python hex2bin.py app.hex app.bin [起始地址]

起始地址默认取 hex 中最小的地址；空洞按 0xFF 填充（与 Flash 擦除态一致）。
"""
import sys


def parse_hex(path):
    mem = {}
    base = 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line.startswith(':'):
                continue
            count = int(line[1:3], 16)
            offset = int(line[3:7], 16)
            rtype = int(line[7:9], 16)
            data = line[9:9 + count * 2]
            if rtype == 0x00:
                for i in range(count):
                    mem[base + offset + i] = int(data[i * 2:i * 2 + 2], 16)
            elif rtype == 0x04:
                base = int(data, 16) << 16
            elif rtype == 0x01:
                break
    return mem


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    src, dst = sys.argv[1], sys.argv[2]
    mem = parse_hex(src)
    if not mem:
        print('hex 为空')
        sys.exit(2)

    start = int(sys.argv[3], 0) if len(sys.argv) > 3 else min(mem)
    end = max(mem)
    buf = bytearray(b'\xFF' * (end - start + 1))
    for addr, val in mem.items():
        if addr < start:
            print(f'警告: 0x{addr:08X} 低于起始地址，已忽略')
            continue
        buf[addr - start] = val

    with open(dst, 'wb') as f:
        f.write(buf)
    print(f'{src} -> {dst}')
    print(f'  起始地址: 0x{start:08X}   长度: {len(buf)} 字节 ({len(buf)/1024:.1f} KB)')


if __name__ == '__main__':
    main()
