# Yoke-motor

ESP-IDF driver for a brushed DC motor driven by an H-bridge on an MCPWM
peripheral. The low-level PWM handling uses the Espressif
[`espressif/bdc_motor`](https://components.espressif.com/components/espressif/bdc_motor)
component.

## Installation

```yaml
dependencies:
  LiangXiaoWei123/yoke_motor: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it.

## Usage

```c
#include "yoke_motor.h"

yoke_motor_handle_t motor;
yoke_motor_config_t config = YOKE_MOTOR_DEFAULT_CONFIG();
config.pwma_gpio_num = GPIO_NUM_19;
config.pwmb_gpio_num = GPIO_NUM_20;
ESP_ERROR_CHECK(yoke_motor_init(&motor, &config));

yoke_motor_set_speed_percent(&motor, 60);
yoke_motor_forward(&motor);       // spin forward at 60%
yoke_motor_reverse(&motor);       // spin in reverse
yoke_motor_brake(&motor);         // slow-decay stop
yoke_motor_coast(&motor);         // fast-decay stop
```

## API

See [`include/yoke_motor.h`](include/yoke_motor.h). Speeds are set as a percent
(0-100) of the configured PWM period.

## Requirements

* ESP-IDF 5.3 or later
* An ESP32 target with an MCPWM peripheral and two GPIO outputs for the
  H-bridge

## Example

See [`examples/motor_demo`](examples/motor_demo).
