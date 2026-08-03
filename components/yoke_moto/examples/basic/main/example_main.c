#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "yoke_moto.h"

void app_main(void)
{
    yoke_moto_t moto = {0};
    yoke_moto_config_t config = yoke_moto_default_config();
    config.pwma_gpio_num = GPIO_NUM_17;
    config.pwmb_gpio_num = GPIO_NUM_18;
    ESP_ERROR_CHECK(yoke_moto_init(&moto, &config));

    ESP_ERROR_CHECK(yoke_moto_forward(&moto));
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_ERROR_CHECK(yoke_moto_coast(&moto));
}
