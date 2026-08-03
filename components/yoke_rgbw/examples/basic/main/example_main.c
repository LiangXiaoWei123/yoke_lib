#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "yoke_rgbw.h"

void app_main(void)
{
    yoke_rgbw_config_t config = yoke_rgbw_default_config();
    config.gpio_num = GPIO_NUM_13;
    config.led_num = 4;
    ESP_ERROR_CHECK(yoke_rgbw_init(&config));

    while (true) {
        ESP_ERROR_CHECK(yoke_rgbw_set_all(255, 0, 0, 0));
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_ERROR_CHECK(yoke_rgbw_set_all(0, 255, 0, 0));
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_ERROR_CHECK(yoke_rgbw_set_all(0, 0, 255, 0));
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_ERROR_CHECK(yoke_rgbw_set_all(0, 0, 0, 255));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
