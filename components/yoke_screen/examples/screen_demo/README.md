# Screen demo

Demonstrates the Yoke-Screen driver: initializes the ST7789 display, the
CST816T touch controller, and LVGL, then renders a counter that updates once
per second.

## Hardware

Requires a RootMaker Yoke board (or one with the same pinout): the ST7789 is
on SPI3 and the CST816T touch controller is on I2C1. Board pins are fixed by
`board_pins.h` in the component and cannot be changed at runtime.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

After boot the screen shows the demo label with an incrementing counter.
