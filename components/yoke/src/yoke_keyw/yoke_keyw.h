/**
 * @file yoke_keyw.h
 * @brief Yoke-KEYW button and GPIO indicator LED driver.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "iot_button.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*yoke_keyw_event_cb_t)(button_event_t event, void *user_data);

typedef struct {
    gpio_num_t button_gpio_num;
    uint8_t button_active_level;
    gpio_num_t led_gpio_num;
    bool led_initial_on;
    uint16_t long_press_time_ms;
} yoke_keyw_config_t;

typedef struct {
    void *button_handle; /**< Private implementation state; do not access directly. */
    yoke_keyw_event_cb_t event_callbacks[BUTTON_EVENT_MAX];
    void *event_user_data[BUTTON_EVENT_MAX];
    gpio_num_t led_gpio_num; /**< Private implementation state; do not access directly. */
    bool initialized;
} yoke_keyw_t;

/** Return the default configuration: button GPIO17 (active high) and LED GPIO18 (on). */
yoke_keyw_config_t yoke_keyw_default_config(void);

/** Initialize the button hardware and GPIO indicator LED. */
esp_err_t yoke_keyw_init(yoke_keyw_t *keyw, const yoke_keyw_config_t *config);

/** Subscribe to one button event. Only subscribed events reach the application. */
esp_err_t yoke_keyw_register_event_callback(yoke_keyw_t *keyw, button_event_t event,
                                             yoke_keyw_event_cb_t callback, void *user_data);

/** Unsubscribe from one button event. */
esp_err_t yoke_keyw_unregister_event_callback(yoke_keyw_t *keyw, button_event_t event);

/** Set the indicator LED state: true drives the LED GPIO high, false drives it low. */
esp_err_t yoke_keyw_set_led(yoke_keyw_t *keyw, bool on);

/** Release the button device and drive the indicator LED GPIO low. */
esp_err_t yoke_keyw_deinit(yoke_keyw_t *keyw);

#ifdef __cplusplus
}
#endif
