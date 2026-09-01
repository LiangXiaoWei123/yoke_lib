/*
 * Yoke-motor demo: sweep the motor speed in the forward direction, then brake.
 * The motor coasts at init (safe default).
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "yoke_motor.h"

#define MOTOR_PWMA_GPIO GPIO_NUM_19
#define MOTOR_PWMB_GPIO GPIO_NUM_20

static const char *TAG = "motor_demo";

void app_main(void) {
    gpio_num_t motor_pins[] = { MOTOR_PWMA_GPIO, MOTOR_PWMB_GPIO };
    const rootmaker_port_t motor_port = {
        .type = ROOTMAKER_PORT_TYPE_RS_BUS,
        .pin_num = 2,
        .pins = motor_pins,
    };
    yoke_motor_handle_t motor = yoke_motor_init(motor_port);
    ESP_ERROR_CHECK(motor != NULL ? ESP_OK : ESP_FAIL);

    ESP_LOGI(TAG, "Sweeping forward 20 -> 100%% then braking.");
    ESP_ERROR_CHECK(yoke_motor_forward(motor));
    for (uint8_t speed = 20; speed <= 100; speed += 20) {
        ESP_ERROR_CHECK(yoke_motor_set_speed_percent(motor, speed));
        ESP_LOGI(TAG, "forward %u%%", speed);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

    ESP_ERROR_CHECK(yoke_motor_brake(motor));
    ESP_LOGI(TAG, "braked. Reverse for 2 s, then coast.");
    ESP_ERROR_CHECK(yoke_motor_reverse(motor));
    ESP_ERROR_CHECK(yoke_motor_set_speed_percent(motor, 60));
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_ERROR_CHECK(yoke_motor_coast(motor));

    ESP_LOGI(TAG, "done");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
