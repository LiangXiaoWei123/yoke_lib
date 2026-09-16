# RootMaker Utils

ESP-IDF helper component for RootMaker boards. It provides the interface port
model and an I2C master bus pool shared by the peripheral drivers
(`yoke_ec11`, `yoke_motor`, `yoke_rad60`, `yoke_rgbw`, ...).

## Installation

```yaml
dependencies:
  ryzobeezero/rootmaker_utils: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it.

## Port model

A `rootmaker_port_t` describes one physical interface by its pins. The
component does **not** predefine any port instances - declare your own as
`static`/global variables and keep them alive for the lifetime of the device:

```c
#include "rootmaker_utils.h"

static rootmaker_port_t rootmaker_port_1 = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = (gpio_num_t[]){ GPIO_NUM_15, GPIO_NUM_16 },   /* SDA / SCL (or TX/RX, PWMA/PWMB) */
};

static rootmaker_port_t rootmaker_s_bus = {
    .type = ROOTMAKER_PORT_TYPE_S_BUS,
    .pin_num = 2,
    .pins = (gpio_num_t[]){ GPIO_NUM_19, GPIO_NUM_20 },
};
```

Drivers that are port-aware take a `rootmaker_port_t` instead of raw pins, e.g.:

```c
gpio_num_t ec11_pins[] = { GPIO_NUM_15, GPIO_NUM_16 };
const rootmaker_port_t ec11_port = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = ec11_pins,
};
yoke_ec11_handle_t ec11 = yoke_ec11_init(ec11_port);
ESP_ERROR_CHECK(ec11 != NULL ? ESP_OK : ESP_FAIL);
```

## I2C bus pool

`rootmaker_new_i2c_bus()` returns an `i2c_master_bus_handle_t`, creating the
bus only if it does not already exist for that I2C peripheral with the same
SDA/SCL pins. Requesting an `i2c_num` that is already held by different pins
returns `ESP_ERR_INVALID_STATE`. Each successful call acquires one reference;
call `rootmaker_del_i2c_bus()` once for each successful acquisition. The
underlying bus is deleted only when the final reference is released. Acquire
and release buses serially from the application lifecycle manager; I2C
transactions themselves are managed by ESP-IDF.

## API

See [`include/rootmaker_utils.h`](include/rootmaker_utils.h).

## Requirements

* ESP-IDF 5.3 or later
* An ESP32 target with an I2C master peripheral
