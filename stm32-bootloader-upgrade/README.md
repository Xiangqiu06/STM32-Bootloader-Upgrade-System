# STM32F103RCT6 Bootloader 固件升级系统（工程骨架）

三分区固件架构 + 外部 Flash 暂存 + 分片升级 + 回滚兜底，对应项目描述里的全部技术点。
代码基于 **STM32 HAL 库 + CubeMX 生成工程**，三个独立工程共用 `common/` 与 `platform/` 目录。

---

## 1. 分区表（板载 256KB Flash，页 2KB）

| 分区 | 起始地址 | 大小 | 作用 |
| --- | --- | --- | --- |
| Bootloader | `0x08000000` | 24KB | 上电判定、固件搬运、跳转 |
| Reset 恢复出厂 | `0x08006000` | 24KB | APP 不可用时串口下载固件 |
| APP A 区 | `0x0800C000` | 96KB | 实际运行的应用程序 |
| APP B 区 | `0x08024000` | 96KB | 升级前备份旧固件（回滚用） |
| 参数区 | `0x0803C000` | 16KB | 预留（当前参数放 AT24C02） |

> Bootloader 给到 24KB 是因为 HAL 库带了 UART+DMA、SPI、I2C 三个外设驱动，16KB 会比较紧张。
> 参数集中定义在 `common/fw_config.h`，改分区只改这一个文件。

外部 W25Q64：`0x000000` 起为新固件暂存区（最大 1MB），新固件**先落外部 Flash，再进 A 区**，保证升级失败时设备仍能启动旧固件。

## 2. 目录结构

```
common/          三个工程共用
  fw_config.h      分区表、协议常量
  fw_proto.c/h     帧格式 + 流式解析器（抗粘包/半包）
  crc_utils.c/h    CRC16(帧) / CRC32(整包，与 zlib 一致)
  fw_param.c/h     AT24C02 参数与升级标志（主备双份）
  fw_update.c/h    升级状态机：收包 -> 写 W25Q64 -> 校验 -> 置标志 -> 复位
platform/        三个工程共用
  bsp_flash.c/h    内部 Flash 擦写（2KB 页 / 半字编程）
  bsp_w25q64.c/h   W25Q64 驱动（SPI，ID/读/擦/写/CRC）
  bsp_at24c02.c/h  AT24C02 驱动（**软件 I2C，PA2/PA3**，按 8 字节页写）
  bsp_uart_dma.c/h UART + DMA + 空闲中断 + 环形缓冲
bootloader/      Bootloader 工程
  boot_main.c     上电判定主流程
  boot_jump.c/h   合法性检查 + 跳转
application/     APP 工程
  app_main.c      示例应用（运行灯 + 串口接收升级）
reset/           Reset 恢复出厂工程
  reset_main.c    串口等待固件
tools/
  fw_send.py      上位机分片发送脚本
```

## 3. 硬件接线（已按野火 F103-MINI 原理图核对）

| 功能 | 引脚 | 说明 |
| --- | --- | --- |
| USART1 TX/RX | **PA9 / PA10** | 板载 CH340，插 USB 线即可，115200 8N1 |
| W25Q64 | SCK **PA5**、MISO **PA6**、MOSI **PA7**、CS **PA4** | 板载 8MB Flash，WP#/HOLD# 已上拉 |
| TF 卡 CS | **PA8** | 与 W25Q64 共用 SPI1，必须保持高电平 |
| AT24C02 | SCL **PA2**、SDA **PA3** | **软件 I2C**（见下方说明），地址 0x50 |
| LED1 / LED2 | **PC3 / PC4** | 共阳接法：低电平点亮 |
| KEY1 / KEY2 | **PA0 / PC13** | 按键按下为低（可选，做"长按进入升级模式"） |

### 为什么 EEPROM 改成软件 I2C

原理图上 AT24C02 的 SCL/SDA 接的是 **PA2/PA3**，而 STM32F103 的硬件 I2C1 只有 PB6/PB7 和 PB8/PB9 两套引脚；这块板的 PB6/PB7 已经被 `LCD_D6/LCD_D7` 占用。所以本工程用**软件模拟 I2C** 驱动 EEPROM（`bsp_at24c02.c` 已按此实现，CubeMX **不需要**开 I2C1 外设）。

## 4. CubeMX 配置（三个工程保持一致）

1. RCC：HSE 外部晶振 8MHz，时钟树 72MHz。
2. USART1：Asynchronous，115200，**DMA：USART1_RX 设为 Circular**，NVIC 使能 `USART1 global interrupt`。
3. SPI1：Full-Duplex Master，8bit，预分频 8（APB2 72MHz / 8 = 9MHz），软件 NSS。
4. GPIO 输出：
   - **PA4（FLASH_CS）** 推挽输出，默认高
   - **PA8（TF_CS）** 推挽输出，默认高 ← 少了这条 W25Q64 读 ID 会失败
   - **PA2 / PA3** 开漏输出 + 上拉（软件 I2C 的 SCL/SDA，板上已有 4.7K 上拉）
   - **PC3 / PC4（LED1/LED2）** 推挽输出，默认高（灭）
5. **不要**生成 I2C1 外设（用不到）。
6. 生成时选 **Copy only necessary library files**，Toolchain 选 MDK-ARM。

## 5. 三个 Keil 工程从哪来 + 怎么把代码加进去

### 5.1 先纠正一个理解

**"三个工程"不是现成给你的，要你自己建三份独立的 CubeMX/Keil 工程**，分别烧到 Flash 的三个位置。你截图里的 `demo1`（Core / Drivers / MDK-ARM）就是其中**一份**，而且从工程树看 gpio/dma/spi/usart 都配好了，配置是对的。

推荐把代码和三个工程放同一个父目录下：

```
我的升级系统/
├── stm32-bootloader-upgrade/   ← 我给你的源码（common/ platform/ 等，原样不动）
├── demo_boot/                  ← 工程一：Bootloader（复制 demo1 改名）
├── demo_reset/                 ← 工程二：Reset 恢复出厂（复制 demo1 改名）
└── demo_app/                   ← 工程三：APP（你现在的 demo1，改名或直接用）
```

### 5.2 复制出三份工程（1 分钟）

1. 把 `demo1` 整个文件夹**复制三份**，分别改名 `demo_boot`、`demo_reset`、`demo_app`（`.ioc`、`MDK-ARM` 一起复制）。
2. 三个工程的外设配置完全相同，不用回 CubeMX 重新配；以后要改引脚就分别打开各自的 `.ioc`。

> 不想复制也行：CubeMX 里 File → New Project 重新生成两次，配置照第 4 节配。复制更快。

### 5.3 往每个 Keil 工程里加源码（以 demo_boot 为例）

打开 `demo_boot/MDK-ARM/` 里的 `.uvprojx`：

1. **头文件路径**：Options for Target → C/C++ → Include Paths，新增三条（相对路径按你的实际层级，上面推荐结构就是 `..\..\stm32-bootloader-upgrade\common` 这样）：
   - `stm32-bootloader-upgrade/common`
   - `stm32-bootloader-upgrade/platform`
   - `stm32-bootloader-upgrade/bootloader`
2. **加源文件**：工程树右键 → Add Group，建三个组，再 Add Existing Files：
   - 组 `common`：`crc_utils.c`、`fw_proto.c`、`fw_param.c`、`fw_update.c`
   - 组 `platform`：`bsp_flash.c`、`bsp_w25q64.c`、`bsp_at24c02.c`、`bsp_uart_dma.c`
   - 组 `boot`：`boot_main.c`、`boot_jump.c`
3. **改 Flash 布局**：Options → Target → IROM1 按下表填，并勾选 `Use Memory Layout from Target Dialog`。

三个工程各不相同的地方只有两处——**加哪个"专属"文件**和 **IROM1**：

| 工程 | 专属源码（加进组里） | IROM1 Start | IROM1 Size | main.c 调用 |
| --- | --- | --- | --- | --- |
| demo_boot | `bootloader/boot_main.c`、`boot_jump.c` | `0x08000000` | `0x6000` | `boot_run();` |
| demo_reset | `reset/reset_main.c` | `0x08006000` | `0x6000` | `reset_run();` |
| demo_app | `application/app_main.c` | `0x0800C000` | `0x18000` | `app_run();` |

common 和 platform 四个文件**三个工程都加**（bootloader 用不到 `fw_update.c` 也没关系，编译器不会把没调用的代码链进去多少）。

| 工程 | IROM1 Start | IROM1 Size | IRAM1 Start | IRAM1 Size |
| --- | --- | --- | --- | --- |
| Bootloader | `0x08000000` | `0x6000` (24K) | `0x20000000` | `0xC000` (48K) |
| Reset | `0x08006000` | `0x6000` (24K) | `0x20000000` | `0xC000` |
| APP | `0x0800C000` | `0x18000` (96K) | `0x20000000` | `0xC000` |

### 5.4 demo_app / demo_reset 额外要做的

- `stm32f1xx_it.c` 的 `USART1_IRQHandler` **保持 CubeMX 原样**即可（只需 `HAL_UART_IRQHandler(&huart1);`，不用加 IDLE 处理，见第 6 节）。
- main.c 按第 6 节改，Reset/APP **第一行**必须设 `SCB->VTOR`。

同时在 **Options → Target** 勾选 `Use Memory Layout from Target Dialog`。

## 6. 各工程 main.c 改动

**Bootloader**（只需在 `MX_*_Init()` 之后调用）：

```c
/* USER CODE BEGIN Includes */
#include "boot_main.h"
/* USER CODE END Includes */

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();          /* 含 PA4/PA8/PA2/PA3/PC3/PC4 */
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_SPI1_Init();

  boot_run();          /* 不返回 */
  while (1) {}
}
```

**APP / Reset**：`main()` **第一行**就要设向量表偏移，否则进中断即 HardFault。

```c
/* USER CODE BEGIN Includes */
#include "app_main.h"      /* Reset 工程改成 "reset_main.h" */
#include "fw_config.h"
/* USER CODE END Includes */

int main(void)
{
  SCB->VTOR = APP_A_START_ADDR;   /* Reset 工程写 RESET_START_ADDR */
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_SPI1_Init();

  app_run();           /* Reset 工程改成 reset_run(); 均不返回 */
  while (1) {}
}
```

> CubeMX 里把 LED 引脚（PC3）的用户标签设为 `LED`，生成的 `main.h` 才会有 `LED_Pin` / `LED_GPIO_Port`，否则 `boot_main.c` 末尾那句编译不过（改成你自己的宏即可）。

**stm32f1xx_it.c**：不用改。`USART1_IRQHandler` 保持 CubeMX 生成的 `HAL_UART_IRQHandler(&huart1);` 就行——本工程**不用 IDLE 中断**收帧，改由主循环轮询 DMA 位置（`bsp_uart_poll()`，已在 `app_main.c` / `reset_main.c` 的主循环里调好）。

> 为什么不用 IDLE：实测中大帧（1039 字节）结束后 IDLE 可能不触发，整帧卡在 DMA 缓冲里，直到下一帧到来才被搬走 → 上位机看到 ACK 迟到 2 秒多。轮询直接读 DMA 计数器，不依赖任何中断触发条件，逻辑还更简单。详见 `bsp_uart_dma.c` 里 `bsp_uart_poll()` 的注释。

## 7. 编译与烧录顺序

1. 编译 Bootloader → 下载到 `0x08000000`。
2. 编译 Reset → 下载到 `0x08006000`（Keil 按第 5 节设置即可自动定位）。
3. 编译 APP → 下载到 `0x0800C000`，复位后应看到 APP 运行灯（500ms 翻转）与串口打印 `[APP] APP v1.0.0 running`。
4. 生成升级用的 bin 文件：

```bash
# Keil
fromelf --bin --output app.bin .\Objects\app.axf
# 或 CubeIDE / arm-none-eabi
arm-none-eabi-objcopy -O binary app.axf app.bin
```

## 8. 升级测试

```bash
pip install pyserial
python tools/fw_send.py --port COM3 --file app.bin --version 1.1
```

预期串口输出：

```
[UP] start, size=xxxx bytes, erasing...
[UP] ready
[APP] progress 10% ... 100%
[UP] verify...
[UP] done, rebooting to bootloader
[BOOT] update request, size=xxxx
[BOOT] backup A -> B ...
[BOOT] migrate W25Q64 -> A ...
[BOOT] update success
[BOOT] jump to APP @0x0800C000
[APP] APP v1.1.0 running @0x0800C000
```

## 9. 协议速查

| 方向 | CMD | PAYLOAD |
| --- | --- | --- |
| 上位机→设备 | `0x01` START | `fw_size(4) fw_crc32(4) version(2) chunk(2)` |
| 上位机→设备 | `0x02` DATA | `seq(2) offset(4) len(2) + data` |
| 上位机→设备 | `0x03` END | `fw_size(4) fw_crc32(4)` |
| 设备→上位机 | `0x81` ACK | `cmd(1) status(1) seq(2)` |

帧格式：`AA 55 | CMD | LEN(2,LE) | PAYLOAD | CRC16(2,LE)`，CRC16 覆盖 `CMD+LEN+PAYLOAD`，多项式 0xA001，初值 0xFFFF。

## 10. 可靠性机制（答辩可讲）

- **整包 CRC32**：上位机算、设备收完重算，两端必须一致。
- **单帧 CRC16 + 序号 + 重传**：单包出错只重发一包，不用整包重来。
- **先暂存后搬运**：新固件完整落到 W25Q64 才写标志，断电不会破坏 A 区。
- **升级前备份**：A 区有效时先备份到 B 区，搬运或启动失败可回滚。
- **启动计数**：Bootloader 每次跳转 `boot_try++`，APP 启动成功后清零；连续 3 次失败自动从 B 区回滚。
- **兜底分区**：A 区彻底损坏时进 Reset 程序，串口重新下载。

## 11. 常见问题

| 现象 | 原因 |
| --- | --- |
| 跳转后进 HardFault | APP 未设置 `SCB->VTOR`，或 Keil IROM 未改 |
| 设备无任何响应 | 波特率/接线错；DMA 未设为 Circular；主循环里漏调 `bsp_uart_poll()` |
| `W25Q64 id=0xFFFFFF` | **PA8（TF_CS）没配成输出高**，TF 卡在 MISO 上抢总线；或 SPI 模式/CS 引脚错 |
| START 就返回 `擦除失败` | W25Q64 不在位，或 `w25q_erase_region` 超时 |
| EEPROM 读写全 0xFF / 超时 | PA2/PA3 没配成开漏输出；或板上 J7 跳帽把 PA2/PA3 引到别处 |
| 升级成功后仍跑旧固件 | 参数没写进 AT24C02（地址 0xA0 无应答说明软件 I2C 时序不对），或 boot_try 已超限 |
| 一打开串口设备就复位/停住 | 板载一键下载电路：`fw_send.py` 已把 DTR/RTS 置无效；用其他串口工具时要手动关掉这两个信号 |
| 升级过程串口乱码 | 时钟不是 72MHz，`SystemClock_Config` 与实际 8MHz 晶振不匹配 |

## 12. 扩展到 CAN / LoRa / 4G-MQTT

`fw_update_feed()` 是唯一入口，链路层只需实现"收字节 → 丢进来"和"发 ACK 字节"两件事：

```c
/* 4G 模组 MQTT 收到一包数据后 */
fw_update_feed(mqtt_payload, mqtt_len);
```

协议、校验、写 Flash、置标志全部复用，这就是"多链路升级"最省事的做法。

## 13. 升级闭环实测（基础链路跑通后做）

前提：设备已用 `merged_all.hex` 烧录，串口能看到 `[APP] ready, send firmware via UART to upgrade`。

```bash
# 1. 改版本号（common/fw_config.h 的 FW_APP_VERSION），重新编译 demo_app
# 2. hex 转 bin（升级协议传的是纯 bin，不是 hex）
python tools/hex2bin.py demo_app/MDK-ARM/demo1/demo1.hex app_v11.bin 0x0800C000

# 3. 关掉串口助手（释放 COM 口），发送固件
python tools/fw_send.py --port COM12 --file app_v11.bin --version 1.1
```

预期现象（按顺序）：

1. 串口打印 `[UP] erase...` → `[APP] progress 10% ... 100%`
2. `[UP] done, rebooting to bootloader`，脚本提示"升级成功"
3. 设备复位后 `[BOOT] update request, size=19904`
4. `[BOOT] backup A -> B ...`（备份旧固件，约 5~10 秒）
5. `[BOOT] migrate W25Q64 -> A ...` → `[BOOT] update success`
6. 跳转后 APP 打印 **新版本号** `APP v1.1.0`

注意事项：

- **FlyMcu 会整片擦除**，不能用它分三次烧三个 hex；要么烧 `merged_all.hex`，要么走上面这条升级链路。
- 升级过程中**不要断电**。中途断电的话，`update_req` 已置位但 A 区可能半写，此时 Bootloader 会在搬运前校验 CRC，失败则从 B 区回滚。
- 若第 4 步备份很慢属正常（内部 Flash 96KB 擦写），并不是卡死。
