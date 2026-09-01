# Yoke-EC11

ESP-IDF driver for the RootMaker Yoke-EC11-V10 I2C peripheral: a rotary encoder,
a button, and two onboard WS2812 LEDs controlled through one I2C slave.

## Installation

Add the component to your project's `idf_component.yml`:

```yaml
dependencies:
  LiangXiaoWei123/yoke_ec11: "^1.0.0"
```

Then run `idf.py reconfigure` (or `idf.py build`) to download it from the ESP
Component Registry.

## Usage

The driver acquires or reuses an I2C master bus through `rootmaker_utils` using
the first two pins of the supplied port. Other devices using the same pins can
share that bus.

```c
#include "yoke_ec11.h"

gpio_num_t ec11_pins[] = { GPIO_NUM_15, GPIO_NUM_16 };
const rootmaker_port_t ec11_port = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = ec11_pins,
};

yoke_ec11_handle_t ec11 = yoke_ec11_init(ec11_port);
ESP_ERROR_CHECK(ec11 != NULL ? ESP_OK : ESP_FAIL);

int16_t count;
yoke_ec11_read_encoder_count(ec11, &count);

yoke_ec11_rgb_t color = { .red = 0, .green = 255, .blue = 0 };
yoke_ec11_set_ws2812(ec11, 0, color);
```

## API

See [`include/yoke_ec11.h`](include/yoke_ec11.h) for the full API. Key
operations:

* `yoke_ec11_init()` / `yoke_ec11_deinit()` — attach/remove the I2C device.
* `yoke_ec11_read_encoder_count()` / `yoke_ec11_read_encoder_diff()` — rotary
  encoder position and per-read delta.
* `yoke_ec11_read_key()` — debounced button state and press counter.
* `yoke_ec11_set_ws2812()` / `yoke_ec11_set_ws2812_all()` — drive the two
  onboard LEDs.
* `yoke_ec11_configure_slave_address()` — persist a new low-three-bit slave
  address (takes effect after peripheral reset).

The I2C register map is documented in [`docs/iic.md`](docs/iic.md).

## Requirements

* ESP-IDF 5.3 or later
* An ESP32 target with an I2C master peripheral

## Example

See [`examples/ec11_demo`](examples/ec11_demo).
