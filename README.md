# Yoke-EC11-V10 编码器 Demo

这是一个基于 ESP-IDF 的 Yoke-EC11-V10 I2C 编码器示例工程。驱动已封装为可复用组件，demo 用旋钮调节两颗 WS2812 的颜色与亮度。

## 接线与默认配置

| 项目 | 默认值 |
| --- | --- |
| I2C 控制器 | I2C0（由 `main.c` 创建） |
| SDA | GPIO15 |
| SCL | GPIO16 |
| I2C 速率 | 100 kHz |
| 从机地址 | `0x70` |
| 设备 ID | `0x4543` |

如接线不同，修改 `main/main.c` 中 `init_i2c_bus()` 的 I2C 配置；如地址或通信参数不同，修改 `yoke_ec11_default_config()` 返回的 `config`。

## Demo 功能

`main/main.c` 提供两颗 WS2812 的独立颜色调节：

- 启动默认调节 LED1。
- 短按一次编码器按键，在 LED1 和 LED2 之间切换调节对象。
- 旋转编码器：调整当前选中 WS2812 的 RGB 颜色。
- 两颗 WS2812 直接以 RGB 格式各自保存自己的颜色，互不影响。
- 按住按键并旋转：调整两颗 WS2812 共用的亮度，范围为 0～100%，每格变化 5%；松开后不会切换调节对象。
- 启动时：创建 I2C 总线，并校验设备 ID `0x4543`。

串口日志示例：

```text
I (xxx) yoke_demo: LED1：RGB=(255, 21, 0)
```

其中 `DIFF` 是自上次读取以来的旋转增量。读取 `DIFF` 后，外设会将其清零。

## 驱动模块 API

驱动组件位于 `components/yoke_ec11/`：

- `yoke_ec11.c`：组件实现
- `include/yoke_ec11.h`：对外公共头文件
- `CMakeLists.txt`：组件依赖声明

应用组件通过以下方式引用，无需手动添加头文件路径：

```cmake
idf_component_register(SRCS "main.c" REQUIRES yoke_ec11)
```

组件只管理自己的从机设备，I2C 总线由应用持有。已有共享总线时，直接将它传入：

```c
yoke_ec11_t yoke;
yoke_ec11_config_t config = yoke_ec11_default_config();
ESP_ERROR_CHECK(yoke_ec11_init(&yoke, existing_i2c_bus, &config));
```

主要 API：

| API | 功能 |
| --- | --- |
| `yoke_ec11_init()` | 将设备挂载到已有 I2C 总线 |
| `yoke_ec11_deinit()` | 从 I2C 总线移除本设备 |
| `yoke_ec11_probe()` | 探测当前地址是否有设备响应 |
| `yoke_ec11_read_key()` | 读取按键状态和短按次数 |
| `yoke_ec11_read_key_press_time_ms()` | 读取按键时长（100 ms 分辨率） |
| `yoke_ec11_read_encoder_count()` | 读取 16 位有符号累计计数 CNT |
| `yoke_ec11_read_encoder_diff()` | 读取 16 位有符号增量 DIFF；读取后外设清零 DIFF |
| `yoke_ec11_set_ws2812()` | 设置单颗 WS2812（索引 0/1） |
| `yoke_ec11_set_ws2812_all()` | 一次设置两颗 WS2812 |
| `yoke_ec11_get_ws2812()` | 读取单颗 WS2812 当前 RGB 值 |
| `yoke_ec11_read_ctrl1()` / `yoke_ec11_write_ctrl1()` | 读写 CTRL1 |
| `yoke_ec11_enter_test_mode()` | 写入 `0x5A` 进入测试模式(暂无此功能) |
| `yoke_ec11_configure_slave_address()` | 保存新的从机地址低 3 位到外设 Flash |
| `yoke_ec11_set_active_address()` | 外设复位后，更新主机访问地址 |

## 构建与烧录

先加载 ESP-IDF 环境后执行：

```sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
