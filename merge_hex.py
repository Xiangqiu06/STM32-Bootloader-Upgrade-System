#!/usr/bin/env python3
"""合并多个 Intel HEX 文件为一个 hex（地址不重叠时可直接合并）。
用法: python merge_hex.py out.hex in1.hex in2.hex ...
"""
import sys


def parse_hex(path):
    """返回 {绝对地址: 字节值} 字典"""
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
            if rtype == 0x00:                      # 数据记录
                for i in range(count):
                    mem[base + offset + i] = int(data[i * 2:i * 2 + 2], 16)
            elif rtype == 0x04:                    # 扩展线性地址
                base = int(data, 16) << 16
            elif rtype == 0x01:                    # EOF
                break
            elif rtype in (0x02, 0x05):
                pass                               # 本例未用到
    return mem


def emit_hex(mem, out_path, row=16):
    """按 16 字节一行重新生成连续的 Intel HEX"""
    addrs = sorted(mem)
    # 按连续区间分组
    segments = []
    start = prev = addrs[0]
    for a in addrs[1:]:
        if a == prev + 1:
            prev = a
            continue
        segments.append((start, prev))
        start = prev = a
    segments.append((start, prev))

    def record(rtype, offset, data=b''):
        body = bytes([len(data), (offset >> 8) & 0xFF, offset & 0xFF, rtype]) + data
        csum = (-sum(body)) & 0xFF
        return ':' + body.hex().upper() + f'{csum:02X}'

    lines = []
    cur_base = None
    for seg_start, seg_end in segments:
        a = seg_start
        while a <= seg_end:
            if cur_base is None or (a >> 16) != cur_base:
                cur_base = a >> 16
                lines.append(record(0x04, 0x0000, bytes([cur_base >> 8, cur_base & 0xFF])))
            n = min(row, seg_end - a + 1, (0x10000 - (a & 0xFFFF)))
            chunk = bytes(mem[a + i] for i in range(n))
            lines.append(record(0x00, a & 0xFFFF, chunk))
            a += n
    lines.append(':00000001FF')                   # EOF
    with open(out_path, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return segments


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    out = sys.argv[1]
    mem = {}
    ranges = []
    for path in sys.argv[2:]:
        m = parse_hex(path)
        lo, hi = min(m), max(m)
        overlap = [r for r in ranges if not (hi < r[0] or lo > r[1])]
        if overlap:
            print(f'错误: {path} (0x{lo:08X}-0x{hi:08X}) 与已有数据重叠!')
            sys.exit(2)
        ranges.append((lo, hi))
        print(f'{path}: 0x{lo:08X} - 0x{hi:08X} ({hi - lo + 1} 字节)')
        mem.update(m)
    segs = emit_hex(mem, out)
    print(f'\n合并完成 -> {out}')
    for s, e in segs:
        print(f'  段 0x{s:08X} - 0x{e:08X}')


if __name__ == '__main__':
    main()
