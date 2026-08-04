# Yoke

ESP-IDF hardware drivers for RootMaker Yoke boards.

The component provides drivers for EC11 input, buttons, brushed DC motors,
RAD60 radar, RGBW LEDs, and the display/touchscreen. Include the umbrella
header when using multiple Yoke drivers:

```c
#include "yoke.h"
```

Individual driver headers are also available under `src/`.

## Requirements

* ESP-IDF 5.3 or later
* An ESP32 target supported by the selected driver peripherals

The component manifest resolves its button, motor, LVGL, and ESP LVGL port
dependencies automatically.
