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

Pass any two-pin RootMaker port to the driver. The first pin is the active-low
button and the second pin is the indicator LED.

```c
#include "yoke_keyw.h"

static void on_event(button_event_t event, void *user_data)
{
    ESP_LOGI("demo", "button event: %d", event);
}

yoke_keyw_handle_t key = yoke_keyw_init(rootmaker_rs_port1);
ESP_ERROR_CHECK(key != NULL ? ESP_OK : ESP_FAIL);
ESP_ERROR_CHECK(yoke_keyw_register_event_callback(key, BUTTON_SINGLE_CLICK, on_event, NULL));
```

When only `BUTTON_SINGLE_CLICK` is registered, it runs immediately after every
short release, and a physical double-click invokes it twice. Registering
`BUTTON_DOUBLE_CLICK` enables the button component's click-classification
window: single-click handling is then delayed briefly so a double-click can be
reported as one distinct event.

The button device and LED GPIO are owned by the driver. Deinitialize each
instance with its matching `yoke_keyw_deinit()` call to release its resources.

## API

See [`include/yoke_keyw.h`](include/yoke_keyw.h).

## Requirements

* ESP-IDF 5.3 or later
* An ESP32 target

## Example

See [`examples/keyw_demo`](examples/keyw_demo).
