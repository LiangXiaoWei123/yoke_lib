# Yoke

ESP-IDF hardware drivers for RootMaker Yoke boards.

## Installation

Add the component to your project's `idf_component.yml`:

```yaml
dependencies:
  LiangXiaoWei123/yoke:
    version: "^1.0.0"
```

Then run `idf.py reconfigure` (or `idf.py build`) to download it from the ESP
Component Registry.

The component provides drivers for EC11 input, buttons, brushed DC motors,
RAD60 radar, RGBW LEDs, the display/touchscreen, and the ZXACC-maker PY32
power-management baseboard. Include the umbrella header when using multiple
Yoke drivers:

```c
#include "yoke.h"
```

Individual driver headers are also available under `src/`.

## Multiple KEYW modules

`yoke_keyw` is instance-based. Zero-initialize one driver object per physical
module and give every module its own GPIO pins. Each KEYW instance
automatically reserves a different LEDC channel.

```c
yoke_keyw_t key_a = {0}, key_b = {0};
yoke_keyw_config_t key_a_cfg = yoke_keyw_default_config();
yoke_keyw_config_t key_b_cfg = yoke_keyw_default_config();
key_b_cfg.button_gpio_num = GPIO_NUM_4;
key_b_cfg.led_gpio_num = GPIO_NUM_5;
ESP_ERROR_CHECK(yoke_keyw_init(&key_a, &key_a_cfg));
ESP_ERROR_CHECK(yoke_keyw_init(&key_b, &key_b_cfg));

```

The maximum count is limited by the target's available LEDC hardware channels.
Deinitialize each instance with its matching `yoke_keyw_deinit(&instance)` call
to release its resources. RGBW WS2812 lights should normally be daisy-chained
and controlled as one strip by setting `led_num` accordingly.

## Requirements

* ESP-IDF 5.3 or later
* An ESP32 target supported by the selected driver peripherals

The component manifest resolves its button, motor, LVGL, and ESP LVGL port
dependencies automatically.

## ZXACC-maker

The ZXACC-maker board communicates over I2C at address `0x73`. Its default
pins are SCL=GPIO40 and SDA=GPIO41. To share an existing I2C bus (for example,
when another board peripheral is attached), set `config.i2c_bus` before
initialization. The driver exposes battery voltage, charge state, firmware
version, button power timing, timed wake-up, and shutdown control.

See the [ZXACC-maker I2C interface](src/yoke_zxacc_maker/README.md) for the
address, register map, byte order, and usage example.
