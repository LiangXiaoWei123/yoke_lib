#include <string.h>

#include "button_gpio.h"
#include "driver/ledc.h"
#include "iot_button.h"

#include "yoke_keyw.h"

#define YOKE_KEYW_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define YOKE_KEYW_LEDC_TIMER     LEDC_TIMER_0
#define YOKE_KEYW_LEDC_CHANNEL   LEDC_CHANNEL_0
#define YOKE_KEYW_LEDC_DUTY_MAX  ((1U << 13) - 1U)

static void yoke_keyw_button_callback(void *button_handle, void *user_data)
{
    (void)button_handle;
    yoke_keyw_t *keyw = user_data;
    if (keyw == NULL || keyw->event_cb == NULL) return;

    button_event_t event = iot_button_get_event((button_handle_t)keyw->button_handle);
    switch (event) {
    case BUTTON_PRESS_DOWN:
        keyw->event_cb(YOKE_KEYW_EVENT_PRESS_DOWN, keyw->user_data);
        break;
    case BUTTON_SINGLE_CLICK:
        keyw->event_cb(YOKE_KEYW_EVENT_SINGLE_CLICK, keyw->user_data);
        break;
    case BUTTON_LONG_PRESS_START:
        keyw->event_cb(YOKE_KEYW_EVENT_LONG_PRESS_START, keyw->user_data);
        break;
    case BUTTON_LONG_PRESS_UP:
        keyw->event_cb(YOKE_KEYW_EVENT_LONG_PRESS_UP, keyw->user_data);
        break;
    default:
        break;
    }
}

yoke_keyw_config_t yoke_keyw_default_config(void)
{
    return (yoke_keyw_config_t) {
        .button_gpio_num = GPIO_NUM_17,
        .button_active_level = 1,
        .led_gpio_num = GPIO_NUM_18,
        .led_initial_brightness_percent = 10,
        .long_press_time_ms = 1500,
    };
}

esp_err_t yoke_keyw_set_led_brightness(yoke_keyw_t *keyw, uint8_t percent)
{
    if (keyw == NULL || !keyw->initialized || percent > 100U) return ESP_ERR_INVALID_ARG;
    uint32_t duty = (YOKE_KEYW_LEDC_DUTY_MAX * percent) / 100U;
    esp_err_t ret = ledc_set_duty(YOKE_KEYW_LEDC_MODE, YOKE_KEYW_LEDC_CHANNEL, duty);
    if (ret == ESP_OK) ret = ledc_update_duty(YOKE_KEYW_LEDC_MODE, YOKE_KEYW_LEDC_CHANNEL);
    return ret;
}

esp_err_t yoke_keyw_init(yoke_keyw_t *keyw, const yoke_keyw_config_t *config)
{
    if (keyw == NULL || config == NULL || config->led_initial_brightness_percent > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (keyw->initialized) return ESP_ERR_INVALID_STATE;
    memset(keyw, 0, sizeof(*keyw));

    const ledc_timer_config_t timer_config = {
        .speed_mode = YOKE_KEYW_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = YOKE_KEYW_LEDC_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_config);
    if (ret != ESP_OK) return ret;
    const ledc_channel_config_t channel_config = {
        .gpio_num = config->led_gpio_num,
        .speed_mode = YOKE_KEYW_LEDC_MODE,
        .channel = YOKE_KEYW_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = YOKE_KEYW_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&channel_config);
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
    if (ret != ESP_OK) return ret;

    keyw->button_handle = button;
    keyw->event_cb = config->event_cb;
    keyw->user_data = config->user_data;
    keyw->initialized = true;
    ret = iot_button_register_cb(button, BUTTON_PRESS_DOWN, NULL, yoke_keyw_button_callback, keyw);
    if (ret == ESP_OK) ret = iot_button_register_cb(button, BUTTON_SINGLE_CLICK, NULL,
                                                     yoke_keyw_button_callback, keyw);
    if (ret == ESP_OK) ret = iot_button_register_cb(button, BUTTON_LONG_PRESS_START, NULL,
                                                     yoke_keyw_button_callback, keyw);
    if (ret == ESP_OK) ret = iot_button_register_cb(button, BUTTON_LONG_PRESS_UP, NULL,
                                                     yoke_keyw_button_callback, keyw);
    if (ret == ESP_OK) ret = yoke_keyw_set_led_brightness(keyw, config->led_initial_brightness_percent);
    if (ret != ESP_OK) (void)yoke_keyw_deinit(keyw);
    return ret;
}

esp_err_t yoke_keyw_deinit(yoke_keyw_t *keyw)
{
    if (keyw == NULL || !keyw->initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = iot_button_delete((button_handle_t)keyw->button_handle);
    (void)ledc_stop(YOKE_KEYW_LEDC_MODE, YOKE_KEYW_LEDC_CHANNEL, 0);
    memset(keyw, 0, sizeof(*keyw));
    return ret;
}

const char *yoke_keyw_event_to_string(yoke_keyw_event_t event)
{
    switch (event) {
    case YOKE_KEYW_EVENT_PRESS_DOWN: return "press down";
    case YOKE_KEYW_EVENT_SINGLE_CLICK: return "single click";
    case YOKE_KEYW_EVENT_LONG_PRESS_START: return "long press start";
    case YOKE_KEYW_EVENT_LONG_PRESS_UP: return "long press up";
    default: return "unknown";
    }
}

