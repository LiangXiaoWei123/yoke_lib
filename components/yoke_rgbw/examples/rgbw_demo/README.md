# RGBW demo

Demonstrates the Yoke-RGBW driver: initializes a strip of 4 RGBW WS2812 LEDs
and cycles red, green, blue, white, and off.

## Hardware

| Item | Default |
| --- | --- |
| LED strip data GPIO | GPIO13 |
| LED count | 4 |

Adjust `RGBW_DATA_GPIO` / `RGBW_LED_NUM` in `main/rgbw_demo_main.c` to match
your strip.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
