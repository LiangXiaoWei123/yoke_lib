/*
 * Yoke-RAD60 demo: initialize the radar on UART1 through RS_PORT1 and print
 * the cached detection data once per second.
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rootmaker_utils.h"
#include "yoke_rad60.h"

static const char *TAG = "rad60_demo";

void app_main(void) {
    ESP_ERROR_CHECK(yoke_rad60_init(rootmaker_rs_port1));
    ESP_LOGI(TAG, "RAD60 initialized on RS_PORT1: TX=GPIO%d RX=GPIO%d",
             rootmaker_rs_port1.pins[0], rootmaker_rs_port1.pins[1]);

    while (1) {
        yoke_rad60_radar_data_t data = { 0 };
        esp_err_t ret = yoke_rad60_get_radar_data(&data);
        if (ret == ESP_ERR_NOT_SUPPORTED && !data.is_comm_ok) {
            ESP_LOGE(TAG, "radar communication timeout: no valid UART frame; ");
        } else if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "radar data lock timeout: %s (0x%x)", esp_err_to_name(ret), ret);
        } else if (ret != ESP_OK) {
            ESP_LOGE(TAG, "failed to get radar data: %s (0x%x)", esp_err_to_name(ret), ret);
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
