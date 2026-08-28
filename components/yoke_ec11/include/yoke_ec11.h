/**
 * @file yoke_ec11.h
 * @brief Yoke-EC11-V10 I2C peripheral driver.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "rootmaker_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YOKE_EC11_DEVICE_ID 0x4543U
#define YOKE_EC11_DEFAULT_I2C_ADDRESS 0x70U
#define YOKE_EC11_DEFAULT_I2C_SPEED_HZ 100000U
#define YOKE_EC11_DEFAULT_TIMEOUT_MS 1000U

/** RGB brightness values for one WS2812 LED. */
typedef struct {
    uint8_t red;   /**< Red brightness, from 0 to 255. */
    uint8_t green; /**< Green brightness, from 0 to 255. */
    uint8_t blue;  /**< Blue brightness, from 0 to 255. */
} yoke_ec11_rgb_t;

/** Decoded EC11 button state. Reading it clears the peripheral press_count. */
typedef struct {
    bool pressed;        /**< Current debounced button state; true when pressed. */
    uint8_t press_count; /**< Short presses since the previous read; cleared by the peripheral after reading. */
} yoke_ec11_key_t;

/** Opaque driver instance. Created by yoke_ec11_init(); NULL on failure. */
typedef struct yoke_ec11 *yoke_ec11_handle_t;

/**
 * Attach one Yoke-EC11-V10 to a port. The I2C master bus is created or reused
 * through rootmaker_utils using the port's first two pins (SDA/SCL); the I2C
 * peripheral is resolved automatically (I2C0 first, then I2C1). The slave
 * address defaults to 0x70 and can be changed with
 * yoke_ec11_set_active_address().
 *
 * @param port Port descriptor (pins[0]=SDA, pins[1]=SCL).
 * @return Driver handle, or NULL on failure (the error is logged).
 */
yoke_ec11_handle_t yoke_ec11_init(rootmaker_port_t port);

/** Remove only this device from the I2C bus; the bus remains available to other devices. */
esp_err_t yoke_ec11_deinit(yoke_ec11_handle_t yoke);

/** Check whether the configured slave responds on I2C. */
esp_err_t yoke_ec11_probe(yoke_ec11_handle_t yoke);

/** Read and return the fixed device ID (expected value: 0x4543). */
esp_err_t yoke_ec11_read_device_id(yoke_ec11_handle_t yoke, uint16_t *device_id);

/** Read button state. The peripheral clears the accumulated press_count after reading. */
esp_err_t yoke_ec11_read_key(yoke_ec11_handle_t yoke, yoke_ec11_key_t *key);

/** Return the current or most recent button press duration in milliseconds, with 100 ms resolution. */
esp_err_t yoke_ec11_read_key_press_time_ms(yoke_ec11_handle_t yoke, uint16_t *time_ms);

/** Read the signed, continuously accumulated encoder count. */
esp_err_t yoke_ec11_read_encoder_count(yoke_ec11_handle_t yoke, int16_t *count);

/** Read the signed encoder delta. This operation clears the peripheral delta counter. */
esp_err_t yoke_ec11_read_encoder_diff(yoke_ec11_handle_t yoke, int16_t *diff);

/** Set one WS2812: index 0 is LED1 (farther from the connector), index 1 is LED2 (nearer). */
esp_err_t yoke_ec11_set_ws2812(yoke_ec11_handle_t yoke, uint8_t index, yoke_ec11_rgb_t color);

/** Set both WS2812 LEDs through I2C writes. */
esp_err_t yoke_ec11_set_ws2812_all(yoke_ec11_handle_t yoke, yoke_ec11_rgb_t led1, yoke_ec11_rgb_t led2);

/** Read one WS2812 color; index definitions match yoke_ec11_set_ws2812(). */
esp_err_t yoke_ec11_get_ws2812(yoke_ec11_handle_t yoke, uint8_t index, yoke_ec11_rgb_t *color);

/** Read or write CTRL1; writing 0x5A puts the peripheral into test mode. */
esp_err_t yoke_ec11_read_ctrl1(yoke_ec11_handle_t yoke, uint8_t *value);
esp_err_t yoke_ec11_write_ctrl1(yoke_ec11_handle_t yoke, uint8_t value);
esp_err_t yoke_ec11_enter_test_mode(yoke_ec11_handle_t yoke);

/**
 * Save the low three bits of a new slave address to peripheral flash.
 * According to the device specification, the change takes effect after reset.
 */
esp_err_t yoke_ec11_configure_slave_address(yoke_ec11_handle_t yoke, uint8_t address_low_3bits);

/** Update only the host target address, for example after resetting the peripheral. */
esp_err_t yoke_ec11_set_active_address(yoke_ec11_handle_t yoke, uint8_t device_address);

#ifdef __cplusplus
}
#endif
