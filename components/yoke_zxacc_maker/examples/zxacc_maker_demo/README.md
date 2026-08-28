# ZXACC-maker demo

Demonstrates the Yoke-ZXACC-maker driver: initializes the power-management
baseboard and prints firmware version, battery voltage, charge state, and the
button power timing once per second.

## Hardware

| Item | Default |
| --- | --- |
| I2C port | I2C0 |
| SDA | GPIO41 |
| SCL | GPIO40 |
| 7-bit I2C address | `0x73` |

Adjust `yoke_zxacc_maker_config_t` fields (or the `i2c_bus` handle) to match
your wiring.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Watch the `zxacc_maker_demo` log lines for the board status.
