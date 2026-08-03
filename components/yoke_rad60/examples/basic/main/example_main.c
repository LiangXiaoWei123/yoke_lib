#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

#include "yoke_rad60.h"

static const char *TAG = "yoke_rad60_example";

void app_main(void)
{
    ESP_ERROR_CHECK(yoke_rad60_init(UART_NUM_1, GPIO_NUM_15, GPIO_NUM_16, 921600));

    while (true) {
        yoke_rad60_radar_data_t data;
        ESP_ERROR_CHECK(yoke_rad60_get_radar_data(&data));
        ESP_LOGI(TAG, "comm=%d detected=%d objects=%u", data.is_comm_ok, data.is_detected,
                 data.radar_info.obj_num);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
