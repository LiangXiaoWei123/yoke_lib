# ZXACC-maker I2C 接口

ZXACC-maker 是带 PY32F002B 从机的电源管理底板。ESP32-S3 通过 I2C 读取电池状态，并配置按键电源控制。

## 默认连接

| 项目 | 值 |
| --- | --- |
| I2C 7 位地址 | `0x73` |
| I2C 速率 | 100 kHz |
| ESP32-S3 I2C 端口 | I2C0 |
| SCL | GPIO40 |
| SDA | GPIO41 |

> 地址 `0x73` 是 **7 位地址**。使用 ESP-IDF `i2c_master_*` API 时直接传入 `0x73`，不要左移为读写地址 `0xE6/0xE7`。

可通过 `yoke_zxacc_maker_config_t` 修改地址、引脚和时钟；若 I2C 总线已由应用创建，传入 `config.i2c_bus` 即可共享该总线。

## 寄存器协议

所有多字节数值使用小端序（最低有效字节在前），与
`yoke_zxacc_maker` 驱动和 `py32_test` 例程保持一致。

| 地址 | 访问 | 数据 | 含义 | 对应 API |
| --- | --- | --- | --- | --- |
| `0x00` | 读 | `uint16_t` | PY32 固件版本 | `get_firmware_version()` |
| `0x02` | 读 | `uint8_t` | 充电状态：`0` 充电中，`1` 未充电，`2` 已充满 | `get_status()` |
| `0x03` | 读 | `uint32_t` | 电池电压，单位 mV | `get_status()` |
| `0x07` | 写 | `0x01` | 立即关机 | `shutdown()` |
| `0x08` | 读/写 | `uint16_t` | 按键开机长按时长，单位 ms | `get/set_wakeup_time()` |
| `0x0A` | 读/写 | `uint16_t` | 按键关机长按时长，单位 ms | `get/set_shutdown_time()` |
| `0x0C` | 写 | `uint32_t` | 定时唤醒秒数；写入后底板进入低功耗，到达设定的时间后自动唤醒 | `set_timer_wakeup()` |

`0x07` 与 `0x0C` 会改变供电状态，调试时请确认串口和程序下载工具不会因此断开。

## ESP-IDF 示例

```c
yoke_zxacc_maker_t board = {0};
yoke_zxacc_maker_config_t config = yoke_zxacc_maker_default_config();
ESP_ERROR_CHECK(yoke_zxacc_maker_init(&board, &config));

yoke_zxacc_maker_status_t status;
ESP_ERROR_CHECK(yoke_zxacc_maker_get_status(&board, &status));
printf("battery: %lu mV\n", (unsigned long)status.battery_voltage_mv);
```

