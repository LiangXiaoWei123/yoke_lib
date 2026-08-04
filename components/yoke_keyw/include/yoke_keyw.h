/**
 * @file yoke_keyw.h
 * @brief Yoke-KEYW 按键与 PWM 指示灯驱动。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YOKE_KEYW_EVENT_PRESS_DOWN,
    YOKE_KEYW_EVENT_SINGLE_CLICK,
    YOKE_KEYW_EVENT_LONG_PRESS_START,
    YOKE_KEYW_EVENT_LONG_PRESS_UP,
} yoke_keyw_event_t;

typedef void (*yoke_keyw_event_cb_t)(yoke_keyw_event_t event, void *user_data);

typedef struct {
    gpio_num_t button_gpio_num;
    uint8_t button_active_level;
    gpio_num_t led_gpio_num;
    uint8_t led_initial_brightness_percent;
    uint16_t long_press_time_ms;
    yoke_keyw_event_cb_t event_cb;
    void *user_data;
} yoke_keyw_config_t;

typedef struct {
    void *button_handle; /**< 私有实现，请勿直接使用。 */
    yoke_keyw_event_cb_t event_cb;
    void *user_data;
    bool initialized;
} yoke_keyw_t;

/** 返回默认配置：按键 GPIO17（高电平有效）、LED GPIO18、亮度 10%。 */
yoke_keyw_config_t yoke_keyw_default_config(void);

/** 初始化按键事件和 GPIO PWM 指示灯。 */
esp_err_t yoke_keyw_init(yoke_keyw_t *keyw, const yoke_keyw_config_t *config);

/** 设置 PWM 指示灯亮度，范围为 0～100。 */
esp_err_t yoke_keyw_set_led_brightness(yoke_keyw_t *keyw, uint8_t percent);

/** 释放按键设备并关闭 PWM 输出。 */
esp_err_t yoke_keyw_deinit(yoke_keyw_t *keyw);

/** 返回事件的可读字符串。 */
const char *yoke_keyw_event_to_string(yoke_keyw_event_t event);

#ifdef __cplusplus
}
#endif
