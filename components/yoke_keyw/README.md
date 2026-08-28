# Yoke-KEYW

ESP-IDF driver for the RootMaker Yoke-KEYW module: one button plus a GPIO
indicator LED. The button handling is built on the Espressif
[`espressif/button`](https://components.espressif.com/components/espressif/button)
component.

## Installation

```yaml
dependencies:
  LiangXiaoWei123/yoke_keyw: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it.

## Usage

The driver is instance-based: zero-initialize one `yoke_keyw_handle_t` per physical
module and give every module its own GPIO pins.

```c
#include "yoke_keyw.h"

static void on_event(button_event_t event, void *user_data)
{
    ESP_LOGI("demo", "button event: %d", event);
}

yoke_keyw_handle_t key = {0};
yoke_keyw_config_t config = YOKE_KEYW_DEFAULT_CONFIG();
config.button_gpio_num = GPIO_NUM_17;
config.led_gpio_num = GPIO_NUM_18;
ESP_ERROR_CHECK(yoke_keyw_init(&key, &config));
ESP_ERROR_CHECK(yoke_keyw_register_event_callback(&key, BUTTON_SINGLE_CLICK, on_event, NULL));
```

The button device and LED GPIO are owned by the driver. Deinitialize each
instance with its matching `yoke_keyw_deinit()` call to release its resources.

## API

See [`include/yoke_keyw.h`](include/yoke_keyw.h).

## Requirements

* ESP-IDF 5.3 or later
* An ESP32 target

## Example

See [`examples/keyw_demo`](examples/keyw_demo).
