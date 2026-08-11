/**
 * @file yoke_rgbw.h
 * @brief RGBW WS2812 LED strip driver.
 */

#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t gpio_num; /**< LED strip data GPIO. */
    uint16_t led_num;    /**< Number of LEDs; must be greater than zero. */
} yoke_rgbw_config_t;

/** Initialize an RGBW WS2812 strip using GRBW byte order. */
esp_err_t yoke_rgbw_init(const yoke_rgbw_config_t *config);

/** Release the RMT resources and strip buffer. */
esp_err_t yoke_rgbw_deinit(void);

/** Set every LED to the same RGBW color and transmit it immediately. */
esp_err_t yoke_rgbw_set_all(uint8_t red, uint8_t green, uint8_t blue, uint8_t white);

#ifdef __cplusplus
}
#endif
