# 硬件说明

## 默认 I2C 配置

| 项目 | 默认值 |
| --- | --- |
| I2C 地址 | `0x70` |
| I2C 速率 | 100 kHz |
| 设备 ID | `0x4543` |

模块使用 7 位 I2C 地址。默认地址为 `0x70`；地址的低 3 位可由 `yoke_ec11_configure_slave_address()` 写入模块 Flash，模块复位后生效。

## 接线

| 模块引脚 | ESP32 引脚 | 说明 |
| --- | --- | --- |
| VCC | 3.3 V | 电源 |
| GND | GND | 共地 |
| SDA | 任意 I2C SDA GPIO | 需要上拉 |
| SCL | 任意 I2C SCL GPIO | 需要上拉 |

`examples/basic` 默认选择 SDA=GPIO15、SCL=GPIO16，并启用 ESP 芯片内部上拉。对于长线或较高通信速率，建议使用外部上拉电阻。

## LED 位置

- LED1：远离连接器。
- LED2：靠近连接器。
