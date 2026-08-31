/*
 * Yoke-RAD60 demo: initialize the radar on UART1 (GPIO17/18) and print the
 * cached detection data once per second.
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "rootmaker_utils.h"
#include "yoke_rad60.h"

static gpio_num_t radar_pins[] = { GPIO_NUM_17, GPIO_NUM_18 };
static rootmaker_port_t radar_port = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = radar_pins,
};

static const char *TAG = "rad60_demo";

void app_main(void) {
    ESP_ERROR_CHECK(yoke_rad60_init(radar_port));
    ESP_LOGI(TAG, "RAD60 initialized on TX=%d RX=%d", radar_pins[0], radar_pins[1]);

    while (1) {
        yoke_rad60_radar_data_t data;
        if (yoke_rad60_get_radar_data(&data) != ESP_OK) {
            ESP_LOGE(TAG, "read failed");
        } else {
            ESP_LOGI(TAG, "comm=%d detected=%d objects=%u", data.is_comm_ok, data.is_detected, data.radar_info.obj_num);
            uint8_t count = data.radar_info.obj_num > 2U ? 2U : data.radar_info.obj_num;
            for (uint8_t i = 0; i < count; ++i) {
                const multitarget_obj_info_t *obj = &data.radar_info.obj[i];
                ESP_LOGI(TAG, "  obj[%u] dist=%ucm angle=%ddeg vel=%dcm/s id=%u", i, obj->range_val, obj->angle_val, obj->velo_val, obj->objid);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
