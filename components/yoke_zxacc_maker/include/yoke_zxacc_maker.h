/**
 * @file yoke_zxacc_maker.h
 * @brief Driver for the ZXACC-maker PY32 power-management baseboard.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YOKE_ZXACC_MAKER_DEFAULT_I2C_ADDR 0x73

typedef enum {
    YOKE_ZXACC_MAKER_CHARGE_CHARGING = 0x00,
    YOKE_ZXACC_MAKER_CHARGE_NOT_CHARGING = 0x01,
    YOKE_ZXACC_MAKER_CHARGE_COMPLETE = 0x02,
    YOKE_ZXACC_MAKER_CHARGE_UNKNOWN = 0xff,
} yoke_zxacc_maker_charge_state_t;

/** Opaque driver instance. Created by yoke_zxacc_maker_init(); NULL on failure. */
typedef struct yoke_zxacc_maker *yoke_zxacc_maker_handle_t;

/**
 * Initialize the power baseboard on the on-board system I2C (SDA=41, SCL=40,
 * address 0x73). The bus is created or reused through rootmaker_utils, so it is
 * shared with the touch panel. Returns handle, or NULL on failure.
 */
yoke_zxacc_maker_handle_t yoke_zxacc_maker_init(void);

/** Remove the I2C device and, if created by this driver, its I2C bus. */
esp_err_t yoke_zxacc_maker_deinit(yoke_zxacc_maker_handle_t board);

/** Read the PY32 firmware version register. */
esp_err_t yoke_zxacc_maker_get_firmware_version(yoke_zxacc_maker_handle_t board, uint16_t *version);

/** Read the battery voltage in millivolts. */
esp_err_t yoke_zxacc_maker_get_battery_voltage(yoke_zxacc_maker_handle_t board, uint32_t *voltage_mv);

/** Read the charge state. */
esp_err_t yoke_zxacc_maker_get_charge_state(yoke_zxacc_maker_handle_t board, yoke_zxacc_maker_charge_state_t *state);

/** Read or set the long-press duration for powering the board on, in ms. */
esp_err_t yoke_zxacc_maker_get_wakeup_time(yoke_zxacc_maker_handle_t board, uint16_t *time_ms);
esp_err_t yoke_zxacc_maker_set_wakeup_time(yoke_zxacc_maker_handle_t board, uint16_t time_ms);

/** Read or set the long-press duration for powering the board off, in ms. */
esp_err_t yoke_zxacc_maker_get_shutdown_time(yoke_zxacc_maker_handle_t board, uint16_t *time_ms);
esp_err_t yoke_zxacc_maker_set_shutdown_time(yoke_zxacc_maker_handle_t board, uint16_t time_ms);

/** Set a timed wake-up. The board enters its low-power state after this write. */
esp_err_t yoke_zxacc_maker_set_timer_wakeup(yoke_zxacc_maker_handle_t board, uint32_t seconds);

/** Request immediate power-off from the PY32 board. */
esp_err_t yoke_zxacc_maker_shutdown(yoke_zxacc_maker_handle_t board);

#ifdef __cplusplus
}
#endif
