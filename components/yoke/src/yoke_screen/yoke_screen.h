#pragma once

#include "esp_lvgl_port.h"
#include "lvgl.h"

lv_display_t* screen_with_lvgl_init(void);

void screen_draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t* data);


