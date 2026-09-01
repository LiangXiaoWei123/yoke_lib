/*
 * Yoke-Screen demo: initialize the 240x240 ST7789 display, the CST816T touch
 * controller, and LVGL, then show a counter that updates once per second.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "yoke_screen.h"

static const char *TAG = "screen_demo";

void app_main(void) {
    lv_display_t *disp = yoke_screen_with_lvgl_init();
    if (disp == NULL) {
        ESP_LOGE(TAG, "screen init failed");
        return;
    }
    ESP_LOGI(TAG, "screen ready, %dx%d", 240, 240);

    lvgl_port_lock(0);
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Yoke-Screen demo");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);
    lvgl_port_unlock();

    uint32_t count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        lvgl_port_lock(0);
        lv_label_set_text_fmt(label, "count: %lu", (unsigned long)count++);
        lvgl_port_unlock();
    }
}
