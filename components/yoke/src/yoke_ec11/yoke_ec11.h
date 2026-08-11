/**
 * @file yoke_ec11.h
 * @brief Yoke-EC11-V10 I2C peripheral driver.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YOKE_EC11_DEVICE_ID              0x4543U
#define YOKE_EC11_DEFAULT_I2C_ADDRESS    0x70U
#define YOKE_EC11_DEFAULT_I2C_SPEED_HZ   100000U
#define YOKE_EC11_DEFAULT_TIMEOUT_MS     1000U

/** RGB brightness values for one WS2812 LED. */
typedef struct {
    uint8_t red;                  /**< Red brightness, from 0 to 255. */
    uint8_t green;                /**< Green brightness, from 0 to 255. */
    uint8_t blue;                 /**< Blue brightness, from 0 to 255. */
} yoke_ec11_rgb_t;

/** Decoded EC11 button state. Reading it clears the peripheral press_count. */
typedef struct {
    bool pressed;                 /**< Current debounced button state; true when pressed. */
    uint8_t press_count;          /**< Short presses since the previous read; cleared by the peripheral after reading. */
} yoke_ec11_key_t;

/** Configuration for one peripheral, used by yoke_ec11_init(). */
typedef struct {
    uint8_t device_address;       /**< Current 7-bit slave address. */
    uint32_t scl_speed_hz;        /**< I2C SCL speed for this device, in Hz. */
    uint32_t timeout_ms;          /**< Timeout for one I2C transaction, in milliseconds. */
    bool verify_device_id;        /**< Read and validate 0x4543 during initialization. */
} yoke_ec11_config_t;

/** Driver instance. Treat all fields as private implementation state. */
typedef struct {
    i2c_master_bus_handle_t bus_handle; /**< I2C master bus handle supplied by the application. */
    i2c_master_dev_handle_t dev_handle; /**< Yoke slave device handle created by this component. */
    uint8_t device_address;             /**< Active 7-bit slave address. */
    uint32_t scl_speed_hz;              /**< Active device communication speed, in Hz. */
    uint32_t timeout_ms;                /**< Active single-transaction timeout, in milliseconds. */
    bool initialized;                   /**< True after successful driver initialization. */
} yoke_ec11_t;

/** Return the default device configuration: address 0x70 at 100 kHz. */
yoke_ec11_config_t yoke_ec11_default_config(void);

/**
 * Attach one Yoke-EC11-V10 to an existing ESP-IDF I2C master bus.
 * The application retains ownership of bus_handle and can add other I2C devices to the bus.
 */
esp_err_t yoke_ec11_init(yoke_ec11_t *yoke,
                          i2c_master_bus_handle_t bus_handle,
                          const yoke_ec11_config_t *config);

/** Remove only this device from the I2C bus; the bus remains available to other devices. */
esp_err_t yoke_ec11_deinit(yoke_ec11_t *yoke);

/** Check whether the configured slave responds on I2C. */
esp_err_t yoke_ec11_probe(const yoke_ec11_t *yoke);

/** Read and return the fixed device ID (expected value: 0x4543). */
esp_err_t yoke_ec11_read_device_id(const yoke_ec11_t *yoke, uint16_t *device_id);

/** Read button state. The peripheral clears the accumulated press_count after reading. */
esp_err_t yoke_ec11_read_key(const yoke_ec11_t *yoke, yoke_ec11_key_t *key);

/** Return the current or most recent button press duration in milliseconds, with 100 ms resolution. */
esp_err_t yoke_ec11_read_key_press_time_ms(const yoke_ec11_t *yoke, uint16_t *time_ms);

/** Read the signed, continuously accumulated encoder count. */
esp_err_t yoke_ec11_read_encoder_count(const yoke_ec11_t *yoke, int16_t *count);

/** Read the signed encoder delta. This operation clears the peripheral delta counter. */
esp_err_t yoke_ec11_read_encoder_diff(const yoke_ec11_t *yoke, int16_t *diff);

/** Set one WS2812: index 0 is LED1 (farther from the connector), index 1 is LED2 (nearer). */
esp_err_t yoke_ec11_set_ws2812(const yoke_ec11_t *yoke, uint8_t index,
                                yoke_ec11_rgb_t color);

/** Set both WS2812 LEDs through I2C writes. */
esp_err_t yoke_ec11_set_ws2812_all(const yoke_ec11_t *yoke,
                                    yoke_ec11_rgb_t led1,
                                    yoke_ec11_rgb_t led2);

/** Read one WS2812 color; index definitions match yoke_ec11_set_ws2812(). */
esp_err_t yoke_ec11_get_ws2812(const yoke_ec11_t *yoke, uint8_t index,
                                yoke_ec11_rgb_t *color);

/** Read or write CTRL1; writing 0x5A puts the peripheral into test mode. */
esp_err_t yoke_ec11_read_ctrl1(const yoke_ec11_t *yoke, uint8_t *value);
esp_err_t yoke_ec11_write_ctrl1(const yoke_ec11_t *yoke, uint8_t value);
esp_err_t yoke_ec11_enter_test_mode(const yoke_ec11_t *yoke);

/**
 * Save the low three bits of a new slave address to peripheral flash.
 * According to the device specification, the change takes effect after reset.
 */
esp_err_t yoke_ec11_configure_slave_address(const yoke_ec11_t *yoke,
                                              uint8_t address_low_3bits);

/** Update only the host target address, for example after resetting the peripheral. */
esp_err_t yoke_ec11_set_active_address(yoke_ec11_t *yoke, uint8_t device_address);

#ifdef __cplusplus
}
#endif
