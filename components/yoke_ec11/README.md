# Yoke-EC11-V10

Yoke-EC11-V10 编码器模块的 ESP-IDF 组件。它通过 I2C 提供编码器计数、按键状态及两颗板载 WS2812 的控制。

## 目录

```text
yoke_ec11/
├── CMakeLists.txt              # ESP-IDF 组件声明
├── idf_component.yml           # 组件元数据与 IDF 版本约束
├── include/yoke_ec11.h         # 公开 API
├── src/yoke_ec11.c             # 驱动实现
├── docs/
│   ├── hardware.md              # 接线与模块约定
│   └── register_map.md          # I2C 寄存器说明
└── examples/basic/              # 可独立构建的最小示例
```

## 接入项目

将本目录复制到目标工程的 `components/yoke_ec11/`，并在应用组件中声明依赖：

```cmake
idf_component_register(SRCS "main.c" REQUIRES yoke_ec11)
```

应用创建并持有 I2C 总线；该组件只在该总线上添加和移除自己的从设备：

```c
#include "driver/i2c_master.h"
#include "yoke_ec11.h"

i2c_master_bus_handle_t i2c_bus; /* 由应用创建 */
yoke_ec11_t yoke;
yoke_ec11_config_t config = yoke_ec11_default_config();
config.verify_device_id = true;
ESP_ERROR_CHECK(yoke_ec11_init(&yoke, i2c_bus, &config));
```

默认地址为 `0x70`，默认速率为 100 kHz。详细接线见 [硬件说明](docs/hardware.md)，寄存器定义见 [寄存器表](docs/register_map.md)。

## 示例

进入 `examples/basic` 后加载 ESP-IDF 环境即可独立构建：

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

示例默认使用 I2C0、SDA=GPIO15、SCL=GPIO16；请按实际接线修改 `main/example_main.c`。

## API 行为

- `yoke_ec11_read_encoder_diff()` 会读取并清除外设的 DIFF 计数。
- `yoke_ec11_read_key()` 会读取并清除外设累计的短按次数。
- `yoke_ec11_deinit()` 仅移除本设备，不会删除共享 I2C 总线。
- `yoke_ec11_configure_slave_address()` 保存地址低 3 位；复位模块后再调用 `yoke_ec11_set_active_address()` 更新主机目标地址。
