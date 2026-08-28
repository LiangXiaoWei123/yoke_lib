/*
 * Yoke-KEYW demo: one button plus a GPIO indicator LED. Single click toggles
 * the LED; long press reports a hold event.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "yoke_keyw.h"

#define KEYW_BUTTON_GPIO GPIO_NUM_17
#define KEYW_LED_GPIO GPIO_NUM_18

static const char *TAG = "keyw_demo";

static yoke_keyw_handle_t s_key;

static void on_single_click(button_event_t event, void *user_data) {
    (void)event;
    (void)user_data;
    static bool led_on = false;
    led_on = !led_on;
    ESP_ERROR_CHECK(yoke_keyw_set_led(&s_key, led_on));
    ESP_LOGI(TAG, "single click: LED %s", led_on ? "on" : "off");
}

static void on_long_press_start(button_event_t event, void *user_data) {
    (void)event;
    (void)user_data;
    ESP_LOGI(TAG, "long press started");
}

void app_main(void) {
    yoke_keyw_config_t config = YOKE_KEYW_DEFAULT_CONFIG();
    config.button_gpio_num = KEYW_BUTTON_GPIO;
    config.button_active_level = 1;
    config.led_gpio_num = KEYW_LED_GPIO;
    config.led_initial_on = true;
    config.long_press_time_ms = 1500;

    ESP_ERROR_CHECK(yoke_keyw_init(&s_key, &config));
    ESP_ERROR_CHECK(yoke_keyw_register_event_callback(&s_key, BUTTON_SINGLE_CLICK, on_single_click, NULL));
    ESP_ERROR_CHECK(yoke_keyw_register_event_callback(&s_key, BUTTON_LONG_PRESS_START, on_long_press_start, NULL));

    ESP_LOGI(TAG, "KEYW ready. Single click toggles the LED, long press to log.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
