#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

/**
 * @file yoke_screen.h
 * @brief Display and touch initialization for the RootMaker Yoke board.
 *
 * This driver configures the board's built-in 240x240 ST7789 display,
 * CST816T touch controller, and the ESP LVGL port. Board pins and panel
 * orientation are fixed by the hardware design.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the LCD, touch controller, and LVGL display port.
 *
 * This function owns the board display resources and must be called once
 * before creating LVGL objects. It creates the display's LVGL instance and
 * registers the CST816T touch input device. The component reuses or creates
 * the matching system I2C bus through rootmaker_utils.
 *
 * @return A valid LVGL display handle on success, or NULL if any display,
 *         touch, SPI, I2C, or LVGL initialization step fails.
 *
 * @note The returned handle is owned by this component. Do not delete it.
 * @note Call yoke_screen_deinit() after stopping application LVGL activity if the
 *       display must be shut down.
 */
lv_display_t* yoke_screen_with_lvgl_init(void);

/**
 * @brief Remove the LVGL display and touch devices and release screen hardware.
 *
 * The caller must stop creating or updating LVGL objects before calling this
 * function. The display handle returned by yoke_screen_with_lvgl_init() becomes
 * invalid after this call. The shared system I2C bus is released, but it is
 * deleted only when its final reference is released.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the screen is not
 *         initialized, or the first cleanup error.
 */
esp_err_t yoke_screen_deinit(void);

/**
 * @brief Draw an RGB565 bitmap directly to the LCD.
 *
 * @param x    Horizontal position of the bitmap's top-left corner, in pixels.
 * @param y    Vertical position of the bitmap's top-left corner, in pixels.
 * @param w    Bitmap width in pixels; must be greater than zero.
 * @param h    Bitmap height in pixels; must be greater than zero.
 * @param data RGB565 pixel data in row-major order. The buffer must contain at
 *             least @p w * @p h pixels and remain valid until the LCD driver
 *             has consumed it.
 *
 * @note Call yoke_screen_with_lvgl_init() successfully first.
 * @note The drawable area is 240x240 pixels. Coordinates outside this range
 *       are logged as an error by the current implementation; callers should
 *       always provide an in-bounds rectangle.
 * @note When LVGL is actively rendering, use its locking API around direct
 *       panel access to prevent concurrent display transactions.
 */
void yoke_screen_draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* data);

#ifdef __cplusplus
}
#endif
