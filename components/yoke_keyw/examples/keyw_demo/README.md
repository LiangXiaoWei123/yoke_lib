# KEYW demo

Demonstrates the Yoke-KEYW driver: initializes a button + indicator LED module,
then uses single click to toggle the LED and long press to log.

## Hardware

| Item | Default |
| --- | --- |
| Button GPIO | GPIO17 (active high) |
| LED GPIO | GPIO18 |

Adjust `KEYW_BUTTON_GPIO` / `KEYW_LED_GPIO` in `main/keyw_demo_main.c` to match
your wiring.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
