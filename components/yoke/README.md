# Yoke

ESP-IDF hardware drivers for RootMaker Yoke boards.

The component provides drivers for EC11 input, buttons, brushed DC motors,
RAD60 radar, RGBW LEDs, the display/touchscreen, and the ZXACC-maker PY32
power-management baseboard. Include the umbrella header when using multiple
Yoke drivers:

```c
#include "yoke.h"
```

Individual driver headers are also available under `src/`.

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
