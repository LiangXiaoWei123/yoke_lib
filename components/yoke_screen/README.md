# Yoke-Screen

ESP-IDF driver for the RootMaker Yoke board display: a 240x240 ST7789 LCD on
SPI, a CST816T touch controller on the board system I2C bus, and an LVGL port wired to both. Board
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

lv_display_t *disp = yoke_screen_with_lvgl_init();
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

The touch controller joins the system I2C bus identified by its board pins. If
another component already acquired that bus, the screen driver reuses it.

Call `yoke_screen_deinit()` only after stopping application LVGL activity. It removes
the LVGL display and touch input, releases the touch device, and releases one
reference to the shared system I2C bus. The bus is deleted only when its final
reference is released. Treat this as final cleanup for the current boot
session; the application should not use the returned display handle afterward.

### Direct bitmap drawing

`yoke_screen_draw_bitmap()` writes an RGB565 bitmap straight to the panel. The
drawable area is 240x240. When LVGL is actively rendering, wrap direct panel
access in the LVGL port lock to avoid concurrent display transactions.

## Requirements

* ESP-IDF 5.3 or later
* ESP32-S3 with PSRAM and an SPI peripheral (the ST7789 SPI path uses DMA)

## Example

See [`examples/screen_demo`](examples/screen_demo).
