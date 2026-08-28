# Yoke-Screen

ESP-IDF driver for the RootMaker Yoke board display: a 240x240 ST7789 LCD on
SPI, a CST816T touch controller on I2C1, and an LVGL port wired to both. Board
pins and panel orientation are fixed by the hardware design.

## Installation

```yaml
dependencies:
  LiangXiaoWei123/yoke_screen: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it. The component
resolves `espressif/esp_lvgl_port` and `lvgl/lvgl` automatically.

## Usage

```c
#include "yoke_screen.h"

lv_display_t *disp = screen_with_lvgl_init();
if (disp == NULL) {
    ESP_LOGE("demo", "screen init failed");
    return;
}

// Create your UI from any task, wrapped in the LVGL port lock.
lvgl_port_lock(0);
lv_obj_t *label = lv_label_create(lv_scr_act());
lv_label_set_text(label, "Hello Yoke");
lv_obj_center(label);
lvgl_port_unlock();
```

The touch controller joins the I2C1 bus. If your application already created an
I2C1 master bus (for example the ZXACC-maker power baseboard uses I2C1), the
screen driver reuses it.

### Direct bitmap drawing

`screen_draw_bitmap()` writes an RGB565 bitmap straight to the panel. The
drawable area is 240x240. When LVGL is actively rendering, wrap direct panel
access in the LVGL port lock to avoid concurrent display transactions.

## Requirements

* ESP-IDF 5.3 or later
* ESP32-S3 with PSRAM and an SPI peripheral (the ST7789 SPI path uses DMA)

## Example

See [`examples/screen_demo`](examples/screen_demo).
