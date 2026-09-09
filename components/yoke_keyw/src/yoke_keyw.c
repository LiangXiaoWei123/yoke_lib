/** @file yoke_keyw.c */
#include "yoke_keyw.h"

#include <stdlib.h>
#include <string.h>

#include "button_gpio.h"
#include "iot_button.h"
#include "esp_log.h"

#define TAG "yoke_keyw"

#define YOKE_KEYW_BUTTON_ACTIVE_LEVEL 1
#define YOKE_KEYW_LED_INITIAL_ON false
#define YOKE_KEYW_LONG_PRESS_TIME_MS 1500

struct yoke_keyw {
    void *button_handle;
    yoke_keyw_event_cb_t event_callbacks[BUTTON_EVENT_MAX];
    void *event_user_data[BUTTON_EVENT_MAX];
    rootmaker_port_t port;
    gpio_num_t button_gpio_num;
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
    bool double_click_enabled = keyw->event_callbacks[BUTTON_DOUBLE_CLICK] != NULL;

    if (event == BUTTON_PRESS_UP && !double_click_enabled &&
        keyw->event_callbacks[BUTTON_SINGLE_CLICK] != NULL &&
        iot_button_get_pressed_time((button_handle_t)keyw->button_handle) < keyw->long_press_time_ms) {
        /* Without a double-click subscriber there is no reason to wait for
         * Espressif's multi-click window. Dispatch one single click on every
         * short release, so two quick presses naturally produce two events. */
        keyw->event_callbacks[BUTTON_SINGLE_CLICK](BUTTON_SINGLE_CLICK,
                                                   keyw->event_user_data[BUTTON_SINGLE_CLICK]);
    }

    if ((event == BUTTON_SINGLE_CLICK || event == BUTTON_DOUBLE_CLICK) && !double_click_enabled) {
        return;
    }
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
        .gpio_num = keyw->button_gpio_num,
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

    /* PRESS_UP provides immediate single-click dispatch when double-click is
     * unused. SINGLE/DOUBLE provide delayed classification when it is used. */
    ret = iot_button_register_cb(button, BUTTON_PRESS_UP, NULL, yoke_keyw_button_callback, keyw);
    if (ret == ESP_OK) {
        ret = iot_button_register_cb(button, BUTTON_SINGLE_CLICK, NULL, yoke_keyw_button_callback, keyw);
    }
    if (ret == ESP_OK) {
        ret = iot_button_register_cb(button, BUTTON_DOUBLE_CLICK, NULL, yoke_keyw_button_callback, keyw);
    }
    if (ret != ESP_OK) {
        (void)iot_button_delete(button);
        keyw->button_handle = NULL;
        return ret;
    }

    for (int event = 0; event < BUTTON_EVENT_MAX; event++) {
        if (event == BUTTON_PRESS_UP || event == BUTTON_SINGLE_CLICK || event == BUTTON_DOUBLE_CLICK) {
            continue;
        }
        if (keyw->event_callbacks[event] != NULL) {
            ret = iot_button_register_cb(button, (button_event_t)event, NULL, yoke_keyw_button_callback, keyw);
            if (ret != ESP_OK) {
                (void)iot_button_delete(button);
                keyw->button_handle = NULL;
                return ret;
            }
        }
    }
    return ESP_OK;
}

yoke_keyw_handle_t yoke_keyw_init(rootmaker_port_t port) {
    gpio_num_t button_gpio_num;
    gpio_num_t led_gpio_num;
    esp_err_t ret = rootmaker_get_port_pins(&port, &button_gpio_num, &led_gpio_num);
    if (ret != ESP_OK || button_gpio_num == led_gpio_num) {
        ESP_LOGE(TAG, "invalid port pins");
        return NULL;
    }

    ret = rootmaker_use_port(&port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "port in use: %s", esp_err_to_name(ret));
        return NULL;
    }

    yoke_keyw_handle_t keyw = calloc(1, sizeof(struct yoke_keyw));
    if (keyw == NULL) {
        ESP_LOGE(TAG, "no mem");
        (void)rootmaker_release_port(&port);
        return NULL;
    }
    keyw->port = port;
    keyw->button_gpio_num = button_gpio_num;
    keyw->led_gpio_num = led_gpio_num;
    keyw->long_press_time_ms = YOKE_KEYW_LONG_PRESS_TIME_MS;

    const gpio_config_t led_gpio_config = {
        .pin_bit_mask = 1ULL << keyw->led_gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&led_gpio_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "led gpio config failed: %s", esp_err_to_name(ret));
        (void)rootmaker_release_port(&keyw->port);
        free(keyw);
        return NULL;
    }
    ret = gpio_set_level(keyw->led_gpio_num, YOKE_KEYW_LED_INITIAL_ON ? 1 : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "led level failed: %s", esp_err_to_name(ret));
        (void)rootmaker_release_port(&keyw->port);
        free(keyw);
        return NULL;
    }

    ret = keyw_create_button(keyw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "button init failed: %s", esp_err_to_name(ret));
        (void)gpio_set_level(keyw->led_gpio_num, 0);
        (void)rootmaker_release_port(&keyw->port);
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
    if (keyw->initialized) {
        const uint16_t old_time_ms = keyw->long_press_time_ms;
        keyw->long_press_time_ms = time_ms;
        (void)iot_button_delete((button_handle_t)keyw->button_handle);
        keyw->button_handle = NULL;
        keyw->initialized = false;
        esp_err_t ret = keyw_create_button(keyw);
        if (ret != ESP_OK) {
            keyw->long_press_time_ms = old_time_ms;
            if (keyw_create_button(keyw) == ESP_OK) {
                keyw->initialized = true;
            }
            ESP_LOGE(TAG, "recreate button failed: %s", esp_err_to_name(ret));
            return ret;
        }
        keyw->initialized = true;
    } else {
        keyw->long_press_time_ms = time_ms;
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
    esp_err_t ret = ESP_OK;
    if (event != BUTTON_PRESS_UP && event != BUTTON_SINGLE_CLICK && event != BUTTON_DOUBLE_CLICK) {
        ret = iot_button_register_cb((button_handle_t)keyw->button_handle, event, NULL,
                                     yoke_keyw_button_callback, keyw);
    }
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
    esp_err_t ret = ESP_OK;
    if (event != BUTTON_PRESS_UP && event != BUTTON_SINGLE_CLICK && event != BUTTON_DOUBLE_CLICK) {
        ret = iot_button_unregister_cb((button_handle_t)keyw->button_handle, event, NULL);
    }
    if (ret == ESP_OK) {
        keyw->event_callbacks[event] = NULL;
        keyw->event_user_data[event] = NULL;
    }
    return ret;
}

esp_err_t yoke_keyw_deinit(yoke_keyw_handle_t keyw) {
    if (keyw == NULL)
        return ESP_ERR_INVALID_STATE;
    keyw->initialized = false;
    esp_err_t ret = ESP_OK;
    if (keyw->button_handle != NULL) {
        ret = iot_button_delete((button_handle_t)keyw->button_handle);
    }
    keyw->button_handle = NULL;
    (void)gpio_set_level(keyw->led_gpio_num, 0);
    (void)gpio_reset_pin(keyw->led_gpio_num);
    (void)rootmaker_release_port(&keyw->port);
    free(keyw);
    return ret;
}
