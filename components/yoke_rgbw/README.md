# Yoke-RGBW

ESP-IDF driver for an RGBW WS2812 LED strip driven through RMT. One strip is
supported at a time; all LEDs are set to the same color.

## Installation

```yaml
dependencies:
  LiangXiaoWei123/yoke_rgbw: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it.

## Usage

```c
#include "yoke_rgbw.h"

gpio_num_t rgbw_pins[] = { GPIO_NUM_13 };
const rootmaker_port_t rgbw_port = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 1,
    .pins = rgbw_pins,
};
ESP_ERROR_CHECK(yoke_rgbw_init(rgbw_port, 4));

ESP_ERROR_CHECK(yoke_rgbw_set_all(255, 0, 0, 0));  // red
ESP_ERROR_CHECK(yoke_rgbw_set_all(0, 255, 0, 0));  // green
ESP_ERROR_CHECK(yoke_rgbw_set_all(0, 0, 0, 0));    // off

// Set LED 2 to blue (index is zero-based).
ESP_ERROR_CHECK(yoke_rgbw_set_pixel(2, 0, 0, 255, 0));

ESP_ERROR_CHECK(yoke_rgbw_deinit());
```

Colors use GRBW byte order, as expected by most RGBW WS2812 strips.

## API

See [`include/yoke_rgbw.h`](include/yoke_rgbw.h).

## Requirements

* ESP-IDF 5.3 or later
* An ESP32 target with an RMT peripheral

## Example

See [`examples/rgbw_demo`](examples/rgbw_demo).
