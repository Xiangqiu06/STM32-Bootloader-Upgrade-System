#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
固件升级上位机（串口链路）
用法：
    pip install pyserial
    python fw_send.py --port COM3 --file app.bin --version 1.0

协议：AA 55 | CMD | LEN(2) | PAYLOAD | CRC16(2)
"""

import argparse
import struct
import sys
import time
import zlib

try:
    import serial
except ImportError:
    print("缺少 pyserial，请先执行： pip install pyserial")
    sys.exit(1)

HEAD0, HEAD1 = 0xAA, 0x55
CMD_START, CMD_DATA, CMD_END, CMD_ACK = 0x01, 0x02, 0x03, 0x81
STATUS_TEXT = {
    0: "OK", 1: "参数错误", 2: "擦除失败", 3: "写入失败",
    4: "CRC 校验失败", 5: "状态错误", 6: "长度/大小错误",
}


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else (crc >> 1)
    return crc & 0xFFFF


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    body = struct.pack("<BH", cmd, len(payload)) + payload
    return bytes([HEAD0, HEAD1]) + body + struct.pack("<H", crc16_modbus(body))


class Link:
    def __init__(self, port: str, baud: int):
        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.buf = bytearray()
        # 野火板载 CH340 带 ISP 一键下载电路（DTR/RTS 控制 NRST/BOOT0），
        # 打开串口后必须把这俩信号置为无效，否则设备会被按在复位或进 ISP 模式
        try:
            self.ser.dtr = False
            self.ser.rts = False
        except Exception:
            pass
        time.sleep(0.2)
        self.ser.reset_input_buffer()

    def close(self):
        self.ser.close()

    def send(self, frame: bytes):
        self.ser.write(frame)
        self.ser.flush()

    def read_frame(self, timeout: float):
        """返回 (cmd, payload, seq) 或 None"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            chunk = self.ser.read(256)
            if chunk:
                self.buf.extend(chunk)

            while True:
                start = self.buf.find(bytes([HEAD0, HEAD1]))
                if start < 0:
                    # 丢到最后一个 0xAA 之前，避免把文本日志当成帧
                    if len(self.buf) > 1:
                        self.buf = self.buf[-1:]
                    break
                if start > 0:
                    del self.buf[:start]
                if len(self.buf) < 5:
                    break

                cmd = self.buf[2]
                plen = struct.unpack("<H", self.buf[3:5])[0]
                total = plen + 7
                if plen > 1100:
                    del self.buf[:2]
                    continue
                if len(self.buf) < total:
                    break

                frame = bytes(self.buf[:total])
                del self.buf[:total]
                crc_rx = struct.unpack("<H", frame[-2:])[0]
                if crc_rx != crc16_modbus(frame[2:-2]):
                    continue
                if cmd != CMD_ACK:
                    continue
                ack_cmd, status, seq = struct.unpack("<BBH", frame[5:9])
                self.last_status = status
                return ack_cmd, status, seq
        return None

    def wait_ack(self, ack_cmd: int, seq: int, timeout: float = 3.0):
        res = self.read_frame(timeout)
        if res is None:
            return False, None
        cmd, status, rseq = res
        if cmd == ack_cmd and (seq is None or rseq == seq):
            return status == 0, status
        return False, status


def progress(done: int, total: int):
    pct = done * 100 // total
    bar = "#" * (pct // 4) + "-" * (25 - pct // 4)
    sys.stdout.write(f"\r  [{bar}] {pct:3d}%  {done}/{total} bytes")
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="如 COM3 或 /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--file", required=True, help="固件 bin 文件")
    ap.add_argument("--version", default="1.0", help="版本号，如 1.1")
    ap.add_argument("--chunk", type=int, default=1024, help="分片长度，默认 1024")
    ap.add_argument("--retry", type=int, default=5, help="单包重传次数")
    args = ap.parse_args()

    with open(args.file, "rb") as f:
        fw = f.read()
    if not fw:
        print("固件为空")
        return 1

    ver_parts = [int(x) for x in args.version.split(".")] + [0, 0]
    version = ((ver_parts[0] & 0xFF) << 8) | (ver_parts[1] & 0xFF)
    fw_crc32 = zlib.crc32(fw) & 0xFFFFFFFF

    print(f"固件: {args.file}  大小: {len(fw)} bytes  CRC32: 0x{fw_crc32:08X}  版本: {args.version}")
    link = Link(args.port, args.baud)
    print("连接设备...")

    # ---- 1. START ----
    payload = struct.pack("<IIHH", len(fw), fw_crc32, version, args.chunk)
    ok = False
    for attempt in range(args.retry):
        link.send(build_frame(CMD_START, payload))
        ok, status = link.wait_ack(CMD_START, 0, timeout=5.0)
        if ok:
            break
        print(f"  START 第 {attempt + 1} 次失败: {STATUS_TEXT.get(status, status)}")
        time.sleep(0.3)
    if not ok:
        print("START 失败，设备可能未处于升级模式")
        link.close()
        return 1
    print("设备已擦除暂存区，开始传输")

    # ---- 2. DATA ----
    seq = 0
    offset = 0
    while offset < len(fw):
        chunk = fw[offset:offset + args.chunk]
        hdr = struct.pack("<HIH", seq, offset, len(chunk))
        frame = build_frame(CMD_DATA, hdr + chunk)

        ok = False
        for attempt in range(args.retry):
            link.send(frame)
            ok, status = link.wait_ack(CMD_DATA, seq, timeout=2.0)
            if ok:
                break
        if not ok:
            print(f"\n第 {seq} 包重传 {args.retry} 次仍失败: {STATUS_TEXT.get(status, status)}")
            link.close()
            return 1

        offset += len(chunk)
        seq = (seq + 1) & 0xFFFF
        progress(offset, len(fw))

    print()

    # ---- 3. END ----
    payload = struct.pack("<II", len(fw), fw_crc32)
    ok = False
    for attempt in range(args.retry):
        link.send(build_frame(CMD_END, payload))
        ok, status = link.wait_ack(CMD_END, 0, timeout=10.0)
        if ok:
            break
        print(f"  END 第 {attempt + 1} 次失败: {STATUS_TEXT.get(status, status)}")
        time.sleep(0.3)

    link.close()
    if ok:
        print("升级成功，设备正在重启并搬运固件...")
        return 0
    print("END 校验失败，请重试")
    return 1


if __name__ == "__main__":
    sys.exit(main())
