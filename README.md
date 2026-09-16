# RootMaker ESP-IDF Component Library

[English](README.md) | [中文](README_CN.md)

This repository contains a collection of ESP-IDF components for RootMaker
peripherals. Each peripheral is packaged as an independent component with its
own example project under `examples/`, and every component can be published
independently to the [ESP Component Registry](https://components.espressif.com/).

The root-level `main` directory contains an LVGL-based touchscreen integration
demo for testing the peripheral components.

## Components

| Component | Description | Registry name |
| --- | --- | --- |
| `rootmaker_utils` | Shared interface-port model and I2C bus pool | `ryzobeezero/rootmaker_utils` |
| `yoke_ec11` | EC11 encoder, button, and two onboard WS2812 LEDs | `ryzobeezero/yoke_ec11` |
| `yoke_keyw` | Button and GPIO indicator LED module | `ryzobeezero/yoke_keyw` |
| `yoke_motor` | Brushed DC motor driver using MCPWM | `ryzobeezero/yoke_motor` |
| `yoke_rgbw` | External RGBW WS2812 LED strip | `ryzobeezero/yoke_rgbw` |
| `yoke_rad60` | RAD60 UART radar module | `ryzobeezero/yoke_rad60` |
| `yoke_screen` | ST7789 display, CST816T touch controller, and LVGL | `ryzobeezero/yoke_screen` |
| `yoke_zxacc_maker` | ZXACC-maker PY32 power-management baseboard | `ryzobeezero/yoke_zxacc_maker` |

Each component follows this directory structure:

```text
components/<name>/
├── CMakeLists.txt        # Component build definition
├── idf_component.yml     # Version, dependencies, and registry metadata
├── Kconfig               # Component options (where applicable)
├── README.md
├── CHANGELOG.md
├── LICENSE
├── include/              # Public headers
├── src/                  # Implementation
└── examples/             # Example projects included in the package
```

## Default pins

| Function | Default configuration |
| --- | --- |
| EC11 I2C | SDA=GPIO15, SCL=GPIO16 |
| RGBW LED strip | GPIO13, 4 LEDs |
| RAD60 UART | UART1, TX=GPIO17, RX=GPIO18, 921600 bit/s |
| ZXACC-maker I2C | SDA=GPIO41, SCL=GPIO40, address `0x73` |

> The radar driver's original defaults use GPIO15/16, which conflict with the
> EC11 I2C bus. The root console project therefore uses GPIO17/18 for the radar.
> Default pins can be overridden through macros or initialization parameters in
> the individual component examples.

## Publishing to the ESP Component Registry

`.github/workflows/upload-components.yml` uses
[`espressif/upload-components-ci-action`](https://github.com/espressif/upload-components-ci-action)
to upload all eight components to the `ryzobeezero` namespace:

1. Update `version` in the relevant component's `idf_component.yml` using
   semantic versioning.
2. Commit and push the changes, then create and push a semantic-version tag,
   for example:

   ```sh
   git tag v1.0.0
   git push origin v1.0.0
   ```

3. GitHub Actions starts the upload. Each component version is read from its
   own `idf_component.yml`. Versions already present in the registry are
   skipped, so a tag can release only the components whose versions changed.

Before the first release, register and sign in to the `ryzobeezero` namespace
at [components.espressif.com](https://components.espressif.com/) using GitHub
OAuth, and ensure that the GitHub account has write access to the namespace.

## Build and flash the integration project

After loading the ESP-IDF environment, run:

```sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

For standalone usage, see the project under each component's `examples/`
directory.

## Hardware documentation

- [ZXRoot-Maker-V10 schematic](docs/ZXRoot-Maker-V10.pdf)
- [ZXACC-Maker-V10 schematic](docs/ZXACC-Maker-V10.pdf)
