# KEYW demo

Demonstrates the Yoke-KEYW driver: initializes a button + indicator LED module,
then uses single click to toggle the LED and long press to log.

## Hardware

| Item | Default |
| --- | --- |
| Button GPIO | GPIO17 (active low) |
| LED GPIO | GPIO18 |

The current driver targets the fixed RootMaker Yoke pinout: GPIO17 for the
button and GPIO18 for the indicator LED.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
