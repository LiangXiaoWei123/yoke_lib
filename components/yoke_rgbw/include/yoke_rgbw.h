/**
 * @file yoke_rgbw.h
 * @brief RGBW WS2812 LED strip driver.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "rootmaker_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize an RGBW WS2812 strip on a port (pins[0] = data GPIO) with `led_num` LEDs, using GRBW byte order. */
esp_err_t yoke_rgbw_init(rootmaker_port_t port, uint16_t led_num);

/** Release the RMT resources and strip buffer. */
esp_err_t yoke_rgbw_deinit(void);

/** Set every LED to the same RGBW color and transmit it immediately. */
esp_err_t yoke_rgbw_set_all(uint8_t red, uint8_t green, uint8_t blue, uint8_t white);

/** Set one LED by zero-based index and transmit the complete strip immediately. */
esp_err_t yoke_rgbw_set_pixel(uint16_t index, uint8_t red, uint8_t green, uint8_t blue, uint8_t white);

#ifdef __cplusplus
}
#endif
