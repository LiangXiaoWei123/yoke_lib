/** @file yoke_keyw.c */
#include "yoke_keyw.h"
#include "rootmaker_pin.h"

#include <stdlib.h>
#include <string.h>

#include "button_gpio.h"
#include "iot_button.h"
#include "esp_log.h"

#define TAG "yoke_keyw"

#define YOKE_KEYW_BUTTON_GPIO PORT4_PIN1
#define YOKE_KEYW_BUTTON_ACTIVE_LEVEL 0
#define YOKE_KEYW_LED_GPIO PORT4_PIN2
#define YOKE_KEYW_LED_INITIAL_ON false
#define YOKE_KEYW_LONG_PRESS_TIME_MS 1500

struct yoke_keyw {
    void *button_handle;
    yoke_keyw_event_cb_t event_callbacks[BUTTON_EVENT_MAX];
    void *event_user_data[BUTTON_EVENT_MAX];
    gpio_num_t led_gpio_num;
    uint16_t long_press_time_ms;
    bool initialized;
};

static void yoke_keyw_button_callback(void *button_handle, void *user_data) {
    (void)button_handle;
    yoke_keyw_handle_t keyw = user_data;
    if (keyw == NULL)
        return;
    button_event_t event = iot_button_get_event((button_handle_t)keyw->button_handle);
    if (event < BUTTON_EVENT_MAX && keyw->event_callbacks[event] != NULL) {
        keyw->event_callbacks[event](event, keyw->event_user_data[event]);
    }
}

static esp_err_t keyw_create_button(yoke_keyw_handle_t keyw) {
    const button_config_t button_config = {
        .long_press_time = keyw->long_press_time_ms,
        .short_press_time = 0,
    };
    const button_gpio_config_t gpio_config = {
        .gpio_num = YOKE_KEYW_BUTTON_GPIO,
        .active_level = YOKE_KEYW_BUTTON_ACTIVE_LEVEL,
        .enable_power_save = false,
        .disable_pull = false,
    };
    button_handle_t button;
    esp_err_t ret = iot_button_new_gpio_device(&button_config, &gpio_config, &button);
    if (ret != ESP_OK) {
        return ret;
    }
    keyw->button_handle = button;
    for (int event = 0; event < BUTTON_EVENT_MAX; event++) {
        if (keyw->event_callbacks[event] != NULL) {
            ret = iot_button_register_cb(button, (button_event_t)event, NULL, yoke_keyw_button_callback, keyw);
            if (ret != ESP_OK) {
                return ret;
            }
        }
    }
    return ESP_OK;
}

yoke_keyw_handle_t yoke_keyw_init(void) {
    yoke_keyw_handle_t keyw = calloc(1, sizeof(struct yoke_keyw));
    if (keyw == NULL) {
        ESP_LOGE(TAG, "no mem");
        return NULL;
    }
    keyw->led_gpio_num = YOKE_KEYW_LED_GPIO;
    keyw->long_press_time_ms = YOKE_KEYW_LONG_PRESS_TIME_MS;

    const gpio_config_t led_gpio_config = {
        .pin_bit_mask = 1ULL << keyw->led_gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&led_gpio_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "led gpio config failed: %s", esp_err_to_name(ret));
        free(keyw);
        return NULL;
    }
    ret = gpio_set_level(keyw->led_gpio_num, YOKE_KEYW_LED_INITIAL_ON ? 1 : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "led level failed: %s", esp_err_to_name(ret));
        free(keyw);
        return NULL;
    }

    ret = keyw_create_button(keyw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "button init failed: %s", esp_err_to_name(ret));
        (void)gpio_set_level(keyw->led_gpio_num, 0);
        free(keyw);
        return NULL;
    }
    keyw->initialized = true;
    return keyw;
}

esp_err_t yoke_keyw_set_long_press_time(yoke_keyw_handle_t keyw, uint16_t time_ms) {
    if (keyw == NULL || time_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    keyw->long_press_time_ms = time_ms;
    if (keyw->initialized) {
        iot_button_delete((button_handle_t)keyw->button_handle);
        keyw->button_handle = NULL;
        esp_err_t ret = keyw_create_button(keyw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "recreate button failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t yoke_keyw_set_led(yoke_keyw_handle_t keyw, bool on) {
    if (keyw == NULL || !keyw->initialized)
        return ESP_ERR_INVALID_ARG;
    return gpio_set_level(keyw->led_gpio_num, on ? 1 : 0);
}

esp_err_t yoke_keyw_register_event_callback(yoke_keyw_handle_t keyw, button_event_t event, yoke_keyw_event_cb_t callback, void *user_data) {
    if (keyw == NULL || !keyw->initialized || callback == NULL || event >= BUTTON_EVENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (keyw->event_callbacks[event] != NULL)
        return ESP_ERR_INVALID_STATE;
    esp_err_t ret = iot_button_register_cb((button_handle_t)keyw->button_handle, event, NULL, yoke_keyw_button_callback, keyw);
    if (ret == ESP_OK) {
        keyw->event_callbacks[event] = callback;
        keyw->event_user_data[event] = user_data;
    }
    return ret;
}

esp_err_t yoke_keyw_unregister_event_callback(yoke_keyw_handle_t keyw, button_event_t event) {
    if (keyw == NULL || !keyw->initialized || event >= BUTTON_EVENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (keyw->event_callbacks[event] == NULL)
        return ESP_ERR_NOT_FOUND;
    esp_err_t ret = iot_button_unregister_cb((button_handle_t)keyw->button_handle, event, NULL);
    if (ret == ESP_OK) {
        keyw->event_callbacks[event] = NULL;
        keyw->event_user_data[event] = NULL;
    }
    return ret;
}

esp_err_t yoke_keyw_deinit(yoke_keyw_handle_t keyw) {
    if (keyw == NULL || !keyw->initialized)
        return ESP_ERR_INVALID_STATE;
    esp_err_t ret = iot_button_delete((button_handle_t)keyw->button_handle);
    (void)gpio_set_level(keyw->led_gpio_num, 0);
    free(keyw);
    return ret;
}
