/**
 * @file yoke_keyw.h
 * @brief Yoke-KEYW button and GPIO indicator LED driver.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "iot_button.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*yoke_keyw_event_cb_t)(button_event_t event, void *user_data);

/** Opaque driver instance. Created by yoke_keyw_init(); NULL on failure. */
typedef struct yoke_keyw *yoke_keyw_handle_t;

/**
 * Initialize the KEYW module: button GPIO17 (active low) and indicator LED
 * GPIO18 (off). Long-press threshold defaults to 1500 ms; change it with
 * yoke_keyw_set_long_press_time(). Returns handle, or NULL on failure.
 */
yoke_keyw_handle_t yoke_keyw_init(void);

/** Change the long-press threshold (ms). Applies immediately to an initialized driver. */
esp_err_t yoke_keyw_set_long_press_time(yoke_keyw_handle_t keyw, uint16_t time_ms);

/** Subscribe to one button event. Only subscribed events reach the application. */
esp_err_t yoke_keyw_register_event_callback(yoke_keyw_handle_t keyw, button_event_t event, yoke_keyw_event_cb_t callback, void *user_data);

/** Unsubscribe from one button event. */
esp_err_t yoke_keyw_unregister_event_callback(yoke_keyw_handle_t keyw, button_event_t event);

/** Set the indicator LED state: true drives the LED GPIO high, false drives it low. */
esp_err_t yoke_keyw_set_led(yoke_keyw_handle_t keyw, bool on);

/** Release the button device and drive the indicator LED GPIO low. */
esp_err_t yoke_keyw_deinit(yoke_keyw_handle_t keyw);

#ifdef __cplusplus
}
#endif
