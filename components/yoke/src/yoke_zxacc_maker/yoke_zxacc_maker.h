/**
 * @file yoke_zxacc_maker.h
 * @brief Driver for the ZXACC-maker PY32 power-management baseboard.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YOKE_ZXACC_MAKER_DEFAULT_I2C_ADDR 0x73
#define YOKE_ZXACC_MAKER_DEFAULT_SDA_GPIO GPIO_NUM_41
#define YOKE_ZXACC_MAKER_DEFAULT_SCL_GPIO GPIO_NUM_40

typedef enum {
    YOKE_ZXACC_MAKER_CHARGE_CHARGING = 0x00,
    YOKE_ZXACC_MAKER_CHARGE_NOT_CHARGING = 0x01,
    YOKE_ZXACC_MAKER_CHARGE_COMPLETE = 0x02,
    YOKE_ZXACC_MAKER_CHARGE_UNKNOWN = 0xff,
} yoke_zxacc_maker_charge_state_t;

typedef struct {
    /** Existing I2C master bus. When NULL, the driver creates one. */
    i2c_master_bus_handle_t i2c_bus;
    i2c_port_num_t i2c_port;
    gpio_num_t sda_gpio_num;
    gpio_num_t scl_gpio_num;
    uint8_t i2c_address;
    uint32_t i2c_clock_hz;
} yoke_zxacc_maker_config_t;

typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t i2c_device;
    bool owns_i2c_bus;
    bool initialized;
} yoke_zxacc_maker_t;

/** Default configuration: I2C0, GPIO40/41, address 0x73, and 100 kHz. */
yoke_zxacc_maker_config_t yoke_zxacc_maker_default_config(void);

/** Initialize the board driver and add its I2C device to the bus. */
esp_err_t yoke_zxacc_maker_init(yoke_zxacc_maker_t *board,
                                const yoke_zxacc_maker_config_t *config);

/** Remove the I2C device and, if created by this driver, its I2C bus. */
esp_err_t yoke_zxacc_maker_deinit(yoke_zxacc_maker_t *board);

/** Read the PY32 firmware version register. */
esp_err_t yoke_zxacc_maker_get_firmware_version(yoke_zxacc_maker_t *board,
                                                 uint16_t *version);

/** Read the battery voltage in millivolts. */
esp_err_t yoke_zxacc_maker_get_battery_voltage(yoke_zxacc_maker_t *board,
                                                uint32_t *voltage_mv);

/** Read the charge state. */
esp_err_t yoke_zxacc_maker_get_charge_state(yoke_zxacc_maker_t *board,
                                             yoke_zxacc_maker_charge_state_t *state);

/** Read or set the long-press duration for powering the board on, in ms. */
esp_err_t yoke_zxacc_maker_get_wakeup_time(yoke_zxacc_maker_t *board,
                                            uint16_t *time_ms);
esp_err_t yoke_zxacc_maker_set_wakeup_time(yoke_zxacc_maker_t *board,
                                            uint16_t time_ms);

/** Read or set the long-press duration for powering the board off, in ms. */
esp_err_t yoke_zxacc_maker_get_shutdown_time(yoke_zxacc_maker_t *board,
                                              uint16_t *time_ms);
esp_err_t yoke_zxacc_maker_set_shutdown_time(yoke_zxacc_maker_t *board,
                                              uint16_t time_ms);

/** Set a timed wake-up. The board enters its low-power state after this write. */
esp_err_t yoke_zxacc_maker_set_timer_wakeup(yoke_zxacc_maker_t *board,
                                             uint32_t seconds);

/** Request immediate power-off from the PY32 board. */
esp_err_t yoke_zxacc_maker_shutdown(yoke_zxacc_maker_t *board);

#ifdef __cplusplus
}
#endif
