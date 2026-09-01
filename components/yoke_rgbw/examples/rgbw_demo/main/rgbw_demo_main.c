/*
 * Yoke-RGBW demo: initialize a strip of 4 RGBW LEDs and cycle through
 * red, green, blue, and white, then off.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rootmaker_utils.h"

#include "yoke_rgbw.h"

#define RGBW_DATA_GPIO GPIO_NUM_13
#define RGBW_LED_NUM 4

static const char *TAG = "rgbw_demo";

void app_main(void) {
    gpio_num_t rgbw_pins[] = { RGBW_DATA_GPIO };
    const rootmaker_port_t rgbw_port = {
        .type = ROOTMAKER_PORT_TYPE_RS_BUS,
        .pin_num = 1,
        .pins = rgbw_pins,
    };
    ESP_ERROR_CHECK(yoke_rgbw_init(rgbw_port, RGBW_LED_NUM));

    const struct {
        uint8_t red, green, blue, white;
        const char *name;
    } colors[] = {
        { 255, 0,   0,   0,   "red"   },
        { 0,   255, 0,   0,   "green" },
        { 0,   0,   255, 0,   "blue"  },
        { 0,   0,   0,   255, "white" },
        { 0,   0,   0,   0,   "off"   },
    };

    ESP_LOGI(TAG, "strip on GPIO%d, %d LEDs", RGBW_DATA_GPIO, RGBW_LED_NUM);
    while (1) {
        for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
            ESP_ERROR_CHECK(yoke_rgbw_set_all(colors[i].red, colors[i].green, colors[i].blue, colors[i].white));
            ESP_LOGI(TAG, "%s", colors[i].name);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
