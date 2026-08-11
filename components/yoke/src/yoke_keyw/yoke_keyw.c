#include <string.h>

#include "button_gpio.h"
#include "iot_button.h"

#include "yoke_keyw.h"

static void yoke_keyw_button_callback(void *button_handle, void *user_data)
{
    (void)button_handle;
    yoke_keyw_t *keyw = user_data;
    if (keyw == NULL) return;
    button_event_t event = iot_button_get_event((button_handle_t)keyw->button_handle);
    if (event < BUTTON_EVENT_MAX && keyw->event_callbacks[event] != NULL) {
        keyw->event_callbacks[event](event, keyw->event_user_data[event]);
    }
}

yoke_keyw_config_t yoke_keyw_default_config(void)
{
    return (yoke_keyw_config_t) {
        .button_gpio_num = GPIO_NUM_17,
        .button_active_level = 1,
        .led_gpio_num = GPIO_NUM_18,
        .led_initial_on = true,
        .long_press_time_ms = 1500,
    };
}

esp_err_t yoke_keyw_set_led(yoke_keyw_t *keyw, bool on)
{
    if (keyw == NULL || !keyw->initialized) return ESP_ERR_INVALID_ARG;
    return gpio_set_level(keyw->led_gpio_num, on ? 1 : 0);
}

esp_err_t yoke_keyw_init(yoke_keyw_t *keyw, const yoke_keyw_config_t *config)
{
    if (keyw == NULL || config == NULL || !GPIO_IS_VALID_OUTPUT_GPIO(config->led_gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (keyw->initialized) return ESP_ERR_INVALID_STATE;
    memset(keyw, 0, sizeof(*keyw));

    const gpio_config_t led_gpio_config = {
        .pin_bit_mask = 1ULL << config->led_gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&led_gpio_config);
    if (ret != ESP_OK) return ret;
    ret = gpio_set_level(config->led_gpio_num, config->led_initial_on ? 1 : 0);
    if (ret != ESP_OK) return ret;

    const button_config_t button_config = {
        .long_press_time = config->long_press_time_ms,
        .short_press_time = 0,
    };
    const button_gpio_config_t gpio_config = {
        .gpio_num = config->button_gpio_num,
        .active_level = config->button_active_level,
        .enable_power_save = false,
        .disable_pull = false,
    };
    button_handle_t button;
    ret = iot_button_new_gpio_device(&button_config, &gpio_config, &button);
    if (ret != ESP_OK) {
        (void)gpio_set_level(config->led_gpio_num, 0);
        return ret;
    }

    keyw->button_handle = button;
    keyw->led_gpio_num = config->led_gpio_num;
    keyw->initialized = true;
    return ESP_OK;
}

esp_err_t yoke_keyw_register_event_callback(yoke_keyw_t *keyw, button_event_t event,
                                             yoke_keyw_event_cb_t callback, void *user_data)
{
    if (keyw == NULL || !keyw->initialized || callback == NULL || event >= BUTTON_EVENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (keyw->event_callbacks[event] != NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = iot_button_register_cb((button_handle_t)keyw->button_handle, event,
                                           NULL, yoke_keyw_button_callback, keyw);
    if (ret == ESP_OK) {
        keyw->event_callbacks[event] = callback;
        keyw->event_user_data[event] = user_data;
    }
    return ret;
}

esp_err_t yoke_keyw_unregister_event_callback(yoke_keyw_t *keyw, button_event_t event)
{
    if (keyw == NULL || !keyw->initialized || event >= BUTTON_EVENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (keyw->event_callbacks[event] == NULL) return ESP_ERR_NOT_FOUND;
    esp_err_t ret = iot_button_unregister_cb((button_handle_t)keyw->button_handle, event, NULL);
    if (ret == ESP_OK) {
        keyw->event_callbacks[event] = NULL;
        keyw->event_user_data[event] = NULL;
    }
    return ret;
}

esp_err_t yoke_keyw_deinit(yoke_keyw_t *keyw)
{
    if (keyw == NULL || !keyw->initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = iot_button_delete((button_handle_t)keyw->button_handle);
    (void)gpio_set_level(keyw->led_gpio_num, 0);
    memset(keyw, 0, sizeof(*keyw));
    return ret;
}
