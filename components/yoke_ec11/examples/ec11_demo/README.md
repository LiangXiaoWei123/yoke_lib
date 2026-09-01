# Ec11 demo

Demonstrates the Yoke-EC11 I2C driver: attach the peripheral to an I2C master
bus, verify its device ID, read the encoder and button, and cycle the two
onboard WS2812 LEDs.

## Hardware

| Item | Default |
| --- | --- |
| I2C port | Automatically selected by `rootmaker_utils` |
| SDA | GPIO15 |
| SCL | GPIO16 |

Adjust `EC11_SDA_GPIO` / `EC11_SCL_GPIO` in `main/ec11_demo_main.c` to match
your board. The driver acquires or reuses the matching bus through
`rootmaker_utils`.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The console prints the device ID, encoder count changes, button presses, and
cycles the LEDs.
