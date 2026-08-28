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

const yoke_rgbw_config_t config = {
    .gpio_num = GPIO_NUM_13,
    .led_num = 4,
};
ESP_ERROR_CHECK(yoke_rgbw_init(&config));

ESP_ERROR_CHECK(yoke_rgbw_set_all(255, 0, 0, 0));  // red
ESP_ERROR_CHECK(yoke_rgbw_set_all(0, 255, 0, 0));  // green
ESP_ERROR_CHECK(yoke_rgbw_set_all(0, 0, 0, 0));    // off

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
