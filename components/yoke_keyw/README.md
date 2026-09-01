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

The driver exposes one fixed RootMaker Yoke module: button GPIO17 (active low)
and indicator LED GPIO18.

```c
#include "yoke_keyw.h"

static void on_event(button_event_t event, void *user_data)
{
    ESP_LOGI("demo", "button event: %d", event);
}

yoke_keyw_handle_t key = yoke_keyw_init();
ESP_ERROR_CHECK(key != NULL ? ESP_OK : ESP_FAIL);
ESP_ERROR_CHECK(yoke_keyw_register_event_callback(key, BUTTON_SINGLE_CLICK, on_event, NULL));
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
