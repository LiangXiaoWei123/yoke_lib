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

// Defaults: UART1, TX=GPIO15, RX=GPIO16, 921600 baud.
ESP_ERROR_CHECK(yoke_rad60_init(UART_NUM_1, GPIO_NUM_15, GPIO_NUM_16, 921600));

yoke_rad60_radar_data_t data;
ESP_ERROR_CHECK(yoke_rad60_get_radar_data(&data));
if (data.is_comm_ok && data.is_detected) {
    printf("objects: %u\n", data.radar_info.obj_num);
}
```

The module answers configuration commands too (sensitivity, sense time, light
threshold, radar enable/disable, baud rate, ...). See
[`include/yoke_rad60.h`](include/yoke_rad60.h) for the full command list.

## UART pin conflict note

The RAD60 default pins (GPIO15/16) may conflict with other board peripherals;
pass the actual TX/RX GPIOs to `yoke_rad60_init()` to override them. To change
pins at runtime without restarting the driver, use
`yoke_rad60_reconfigure_uart()`.

## Requirements

* ESP-IDF 5.3 or later
* An ESP32 target with a UART peripheral

## Example

See [`examples/rad60_demo`](examples/rad60_demo).
