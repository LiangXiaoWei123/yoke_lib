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

The driver attaches to an existing ESP-IDF I2C master bus that you create
first. The bus stays owned by your application, so other I2C devices can share
it.

```c
#include "yoke_ec11.h"

i2c_master_bus_config_t bus_cfg = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = GPIO_NUM_15,
    .scl_io_num = GPIO_NUM_16,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};
i2c_master_bus_handle_t bus;
ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

yoke_ec11_handle_t ec11;
yoke_ec11_config_t cfg = YOKE_EC11_DEFAULT_CONFIG();
cfg.verify_device_id = true;
ESP_ERROR_CHECK(yoke_ec11_init(&ec11, bus, &cfg));

int16_t count;
yoke_ec11_read_encoder_count(&ec11, &count);

yoke_ec11_rgb_t color = { .red = 0, .green = 255, .blue = 0 };
yoke_ec11_set_ws2812(&ec11, 0, color);
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
