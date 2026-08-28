# RAD60 demo

Demonstrates the Yoke-RAD60 driver: initializes the radar on UART1 and prints
the cached multi-target detection data once per second.

## Hardware

| Item | Default |
| --- | --- |
| UART port | UART1 |
| TX | GPIO17 |
| RX | GPIO18 |
| Baud rate | 921600 |

Adjust `RADAR_*` macros in `main/rad60_demo_main.c` to match your wiring.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Watch the `rad60_demo` log lines for communication status and object list.
