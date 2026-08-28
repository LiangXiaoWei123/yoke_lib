# Yoke-ZXACC-maker

ESP-IDF driver for the RootMaker ZXACC-maker power-management baseboard: a
PY32F002B I2C slave that reports battery voltage and charge state and controls
button power timing, timed wake-up, and shutdown.

## Installation

```yaml
dependencies:
  LiangXiaoWei123/yoke_zxacc_maker: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it.

## Usage

The driver creates its own I2C0 bus by default (SCL=GPIO40, SDA=GPIO41,
address `0x73`), or it can join an I2C bus your application already created.

```c
#include "yoke_zxacc_maker.h"

yoke_zxacc_maker_handle_t board;
yoke_zxacc_maker_config_t config = YOKE_ZXACC_MAKER_DEFAULT_CONFIG();
ESP_ERROR_CHECK(yoke_zxacc_maker_init(&board, &config));

uint32_t voltage_mv;
yoke_zxacc_maker_get_battery_voltage(&board, &voltage_mv);
yoke_zxacc_maker_charge_state_t charge;
yoke_zxacc_maker_get_charge_state(&board, &charge);

uint16_t wake_ms;
yoke_zxacc_maker_get_wakeup_time(&board, &wake_ms);
yoke_zxacc_maker_set_wakeup_time(&board, 2000);
```

`yoke_zxacc_maker_set_timer_wakeup()` and `yoke_zxacc_maker_shutdown()` change
the board power state, so confirm your serial console and flashing tool will
survive the power cut.

The I2C address is a 7-bit address: pass `0x73` directly to the driver, do not
shift it into the read/write form.

## API

See [`include/yoke_zxacc_maker.h`](include/yoke_zxacc_maker.h). The register
map and byte order are documented in
[`docs/iic.md`](docs/iic.md).

## Requirements

* ESP-IDF 5.3 or later
* An ESP32 target with an I2C master peripheral

## Example

See [`examples/zxacc_maker_demo`](examples/zxacc_maker_demo).
