# STM32 固件升级上位机（Qt5 / Linux）

配套固件：`stm32-bootloader-upgrade`（Bootloader + Reset + APP 三分区方案）。
支持 **UART 串口 / CAN 总线 / LoRa 无线** 三种升级链路，网络通道预留。

## 1. 环境准备（Ubuntu 18.04）

```bash
sudo apt update
sudo apt install -y build-essential cmake qtbase5-dev libqt5serialport5-dev
# CAN 调试工具（可选，用于 candump 观察报文）
sudo apt install -y can-utils
```

Ubuntu 18.04 自带 Qt 5.9.5，满足要求（最低 Qt 5.9）。

## 2. 编译

```bash
cd qt_upgrade_tool
mkdir -p build && cd build
cmake ..
make -j$(nproc)
./fw_upgrade_tool
```

## 3. 界面说明

| 区域 | 内容 |
| --- | --- |
| 1. 升级方式 | 串口 UART / CAN / LoRa / 网络（预留，禁用） |
| 2. 升级目标分区 | APP / RESET / BOOT，自动显示**起始地址**与**分区容量** |
| 3. 固件文件 | 选择 `.bin` 或 `.hex`，实时显示大小与目标分区占用百分比（超限变红并拒绝升级） |
| 4. 操作 | 连接设备 / 开始升级 / 中止，进度条与状态提示 |
| 日志 | 带时间戳的收发过程，含 ACK 重传、失败原因 |

目标分区默认地址（与 `common/fw_config.h` 对应）：

| 目标 | 起始地址 | 分区容量 |
| --- | --- | --- |
| APP | `0x0800C000` | 96 KB |
| RESET | `0x08006000` | 24 KB |
| BOOT | `0x08000000` | 24 KB |

固件先写入 W25Q64 外部 Flash 暂存区，再由 Bootloader 校验并搬运，所以升级中途断电设备仍可用旧固件启动。

### 关于直接选择 .hex 文件

Keil 输出的 `.hex` 是 Intel HEX **文本格式**，文件体积约为实际固件的 2.8 倍（例如 boot.hex 文件 49175 字节，实际固件仅 17464 字节）。工具检测到 `.hex` 后缀会自动解析成纯二进制再校验：

- 支持 Intel HEX 记录类型 00（数据）/ 01（EOF）/ 04（扩展线性地址），并逐行校验和；
- 解析出的基地址若与所选分区起始地址不一致，会弹窗确认（防止选错目标）；
- 界面上会显示「HEX 已解析为二进制，基址 0x0800xxxx」与真实占用率。

## 4. 通信协议

与固件 `common/fw_proto.c` 完全一致：

```
AA 55 | CMD | LEN(2, LE) | PAYLOAD | CRC16(2, LE)
```

- CRC16：Modbus（初值 0xFFFF，多项式 0xA001），覆盖 `CMD + LEN + PAYLOAD`
- 整包校验：CRC32（zlib 多项式，与 `zlib.crc32` 一致）

| CMD | 载荷 | 说明 |
| --- | --- | --- |
| `0x01` START | `<IIHH>` size, crc32, version, chunk | 设备擦除暂存区后回 ACK |
| `0x02` DATA | `<HIH>` seq, offset, len + 数据 | 逐包 ACK，超时重发 |
| `0x03` END | `<II>` size, crc32 | 设备校验整包、写升级标志、复位 |
| `0x81` ACK | `<BBH>` ack_cmd, status, seq | status=0 表成功 |

### 目标分区扩展（重要）

- **APP**：START 载荷 12 字节，**与现有固件完全兼容，可直接使用**
- **RESET / BOOT**：START 载荷末尾追加 1 字节目标号（0=APP，1=RESET，2=BOOT），共 13 字节

现有固件只认 12 字节 START，因此选择 RESET/BOOT 时会返回"参数错误"，工具会提示需要固件升级。
固件侧改动很小（`handle_start()` 接受 12/13 字节并在 `boot_main.c` 里按目标选择搬运地址），
未改动前请只用 APP 目标。

## 5. 各链路使用说明

### UART

1. 板子 USB 线接电脑，`刷新` 后选择 `/dev/ttyUSB0`（CH340）
2. 波特率 115200
3. 连接设备 → 选择固件 → 开始升级

程序内部已把 DTR/RTS 置为无效，避免野火板一键下载电路把 MCU 按在复位状态。

### CAN

```bash
# 需要 root 权限或配置 sudoers
sudo ip link set can0 down
sudo ip link set can0 up type can bitrate 500000
ip -details link show can0
```

工具里选择 `can0`，TX/RX 帧 ID 默认 `0x123` / `0x124`（升级数据按 8 字节拆帧，接收侧按序拼回字节流）。

**注意**：CAN 链路还需要固件侧实现 CAN 收发，并在收到数据后调用 `fw_update_feed()`。
目前固件只有 UART 通道，CAN 属于工具就绪、固件待接。

### LoRa

LoRa 模块按**透传模式**接在串口上，两端参数（空中速率、信道）必须一致。
工具侧默认 9600 波特率、分片 128 字节——空中速率低的时候，分片过大容易触发 ACK 超时。

固件侧无需改动：数据从串口进来后同样走 `fw_update_feed()`。

### 网络（预留）

`NetTransport` 已留好接口，接入 WiFi 模块后在 `open()` / `send()` 里实现：

- TCP：把 `buildFrame()` 的字节流直接 `QTcpSocket::write()`
- MQTT：发布到 topic，载荷同样是完整字节流

`UpgradeSession` 不用改一行——这就是链路抽象的价值。

## 6. 排错

| 现象 | 处理 |
| --- | --- |
| 连接设备失败：Permission denied | 把用户加入 `dialout` 组：`sudo usermod -aG dialout $USER`，重新登录 |
| 一直等 ACK 超时 | 设备是否已进入 APP 并打印 `ready, send firmware via UART`；波特率是否一致 |
| 设备返回"参数错误" | 选了 RESET/BOOT 但固件不支持目标分区（见第 4 节） |
| 设备返回"擦除失败" | W25Q64 未识别（先看 Bootloader 日志有无 `W25Q64 id=` 报错） |
| 升级到 100% 但版本没变 | 看 Bootloader 日志：`backup A -> B` → `migrate` → `update success` 是否都出现 |
| CAN 打不开 | `ip link` 确认接口已 up；`candump can0` 看是否有报文 |

## 7. 目录结构

```
qt_upgrade_tool/
├── CMakeLists.txt
└── src/
    ├── main.cpp                 程序入口
    ├── mainwindow.h/.cpp        界面与交互
    ├── protocol.h/.cpp          帧格式、CRC16/CRC32、流式解析
    ├── transport.h              链路抽象接口
    ├── transport_serial.h/.cpp  串口（UART 与 LoRa 共用）
    ├── transport_can.h/.cpp     Linux SocketCAN
    ├── transport_net.h          网络通道占位
    └── upgradesession.h/.cpp    START/DATA/END 状态机 + 重传
```
