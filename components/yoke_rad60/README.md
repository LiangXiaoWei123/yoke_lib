# Yoke-RAD60

ESP-IDF driver for the RootMaker RAD60 UART radar module. The driver runs its
own UART receive/processing tasks and keeps the latest detection data in a
cache that the application reads non-blocking.

## Installation

```yaml
dependencies:
  LiangXiaoWei123/yoke_rad60: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it.

## Usage

```c
#include "yoke_rad60.h"
#include "rootmaker_utils.h"

static gpio_num_t radar_pins[] = { GPIO_NUM_15, GPIO_NUM_16 };
static rootmaker_port_t radar_port = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = radar_pins,
};

yoke_rad60_radar_data_t data;
ESP_ERROR_CHECK(yoke_rad60_init(radar_port));
ESP_ERROR_CHECK(yoke_rad60_get_radar_data(&data));
if (data.is_comm_ok && data.is_detected) {
    printf("objects: %u\n", data.radar_info.obj_num);
}
```

The component intentionally keeps a narrow surface area now: init, deinit, and
non-blocking reads from the latest cached radar data.

## UART pin conflict note

The RAD60 default pins (GPIO15/16) may conflict with other board peripherals;
pass the actual TX/RX GPIOs through the `rootmaker_port_t` you give to
`yoke_rad60_init()`.

## Requirements

* ESP-IDF 5.3 or later
* An ESP32 target with a UART peripheral

## Example

See [`examples/rad60_demo`](examples/rad60_demo).
