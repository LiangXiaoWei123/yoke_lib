# Motor demo

Demonstrates the Yoke-motor driver: initializes a brushed DC motor, sweeps the
speed forward, brakes, then runs a short reverse burst and coasts.

## Hardware

| Item | Default |
| --- | --- |
| H-bridge PWMA | GPIO19 |
| H-bridge PWMB | GPIO20 |

Adjust `MOTOR_PWMA_GPIO` / `MOTOR_PWMB_GPIO` in `main/motor_demo_main.c` to
match your wiring. The motor coasts at init, so nothing spins until the sweep
starts.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
