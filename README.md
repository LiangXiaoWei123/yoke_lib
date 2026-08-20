# Rootmaker ESP-IDF 组件库

当前工程用于联调 `components/` 下的 Yoke 组件。根目录 `main` 提供一个串口命令控制台，输入一行命令后立即执行。

## 组件

- `yoke_ec11`：EC11 编码器、按键与板载两颗 WS2812。
- `yoke_rad60`：RAD60 UART 雷达模块。
- `yoke_rgbw`：外接 RGBW WS2812 灯带。

## 引脚默认值

| 功能 | 默认引脚 |
| --- | --- |
| EC11 I2C | SDA=GPIO15，SCL=GPIO16 |
| RGBW 灯带 | GPIO13，4 颗灯珠 |
| RAD60 UART | UART1，TX=GPIO17，RX=GPIO18，921600 bit/s |

雷达原驱动默认使用 GPIO15/16，这与 EC11 的 I2C 冲突。因此根工程控制台的雷达默认值改为 GPIO17/18；按实际硬件修改 `main/main.c` 顶部的 `APP_*` 宏，或在 `radar init` 命令中传入引脚。

## 串口命令

烧录后在串口监视器输入 `help` 查看帮助。常用命令：

```text
ec11 info
ec11 key
ec11 count
ec11 led 0 255 0 0

rgbw init
rgbw set 255 0 0 0
rgbw off

radar init
radar read
radar enable 1

# ZXACC-maker（先执行 p 初始化）
p
A
J
N
T
t
```

ZXACC-maker 调试命令：`p` 初始化并探测地址 `0x73`，`A` 扫描 I2C 总线，`J` 读取
电池/充电状态，`N` 读取按键开关机时长，`T` 将两种按键时长写为 2000 ms。`t` 会提示输入
定时唤醒秒数；输入数值并回车后，底板立即进入低功耗，直至定时唤醒。
命令参数以空格分隔；`rgbw init [gpio led_num]` 和 `radar init [uart tx_gpio rx_gpio baudrate]` 可覆盖默认配置。

## 构建与烧录

加载 ESP-IDF 环境后执行：

```sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
