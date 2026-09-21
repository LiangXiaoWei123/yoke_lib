#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file rootmaker_utils.h
 * @brief RootMaker interface port model and I2C bus pool.
 *
 * rootmaker_utils provides two things:
 * 1. A `rootmaker_port_t` descriptor that describes one physical interface
 *    (pins) so drivers can be configured by port instead of raw GPIO numbers.
 *    Port instances are NOT predefined here - each application declares its
 *    own as `static`/global and fills in the pins.
 * 2. An I2C master bus pool keyed by (i2c_num, SDA, SCL). Creating a bus for
 *    the same pins twice returns the existing handle; requesting an i2c_num
 *    that is already held by different pins fails.
 */

/** Interface types. */
typedef enum {
    ROOTMAKER_PORT_TYPE_RS_BUS = 0, /**< General-purpose port (2 signal pins). */
    ROOTMAKER_PORT_TYPE_S_BUS,      /**< S_BUS group. */
    ROOTMAKER_PORT_TYPE_F_BUS,      /**< F_BUS group. */
} rootmaker_port_type_t;

/** One physical interface. Applications define instances and fill the pins. */
typedef struct {
    rootmaker_port_type_t type;
    uint8_t pin_num;  /**< Number of valid pins. */
    gpio_num_t *pins; /**< GPIO array of `pin_num` entries */
} rootmaker_port_t;

/** Predefined RootMaker board ports (pins come from `rootmaker_pin.h`). */
extern const rootmaker_port_t rootmaker_rs_port1;
extern const rootmaker_port_t rootmaker_rs_port2;
extern const rootmaker_port_t rootmaker_rs_port3;
extern const rootmaker_port_t rootmaker_rs_port4;
extern const rootmaker_port_t rootmaker_s_bus;
extern const rootmaker_port_t rootmaker_f_bus;
/** On-board system I2C (SDA=BOADR_SDA, SCL=BOADR_SCL), shared by the power baseboard and the touch panel. */
extern const rootmaker_port_t rootmaker_sys_i2c_port;

/**
 * Read the first two pins of a port.
 *
 * @param port  Port descriptor.
 * @param pin_a Out: pins[0].
 * @param pin_b Out: pins[1].
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if a pin is missing (GPIO_NUM_NC).
 */
esp_err_t rootmaker_get_port_pins(rootmaker_port_t *port, gpio_num_t *pin_a, gpio_num_t *pin_b);

/**
 * Read one pin of a port.
 *
 * @param port  Port descriptor.
 * @param index Pin index within the port (0-based).
 * @param pin   Out: pins[index].
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if the index is out of range or the pin is GPIO_NUM_NC.
 */
esp_err_t rootmaker_get_port_pin(rootmaker_port_t *port, uint8_t index, gpio_num_t *pin);

/**
 * Claim all GPIOs in a port for exclusive use. The claim fails if any GPIO
 * overlaps another exclusive port or an active shared I2C bus. I2C drivers
 * must use rootmaker_new_i2c_bus() instead so SDA/SCL can be shared.
 *
 * @param port Port to claim.
 * @return ESP_OK, ESP_ERR_INVALID_STATE if already claimed, ESP_ERR_NO_MEM if the table is full.
 */
esp_err_t rootmaker_use_port(rootmaker_port_t *port);

/** Release a previously claimed port. ESP_ERR_NOT_FOUND if it was not claimed. */
esp_err_t rootmaker_release_port(rootmaker_port_t *port);

/** Return true when any GPIO in the port is used exclusively or by a shared I2C bus. */
bool rootmaker_port_is_used(rootmaker_port_t *port);

/**
 * Get or create an I2C master bus for a port.
 *
 * The first two pins (SDA/SCL) are acquired for shared I2C use. Acquisition
 * fails if either GPIO is held by an exclusive rootmaker_use_port() claim.
 * Multiple calls with the same SDA/SCL share the bus by reference count.
 *
 * The I2C peripheral is resolved automatically: the first free peripheral is
 * tried in order (I2C0 first, then I2C1). Sharing rules per peripheral:
 * - unused            -> creation is attempted there; externally occupied
 *                        peripherals are skipped.
 * - used with same pins -> the existing handle is returned and its reference
 *   count is incremented.
 * - used with other pins -> the next peripheral is tried.
 * If no peripheral can host the port, ESP_ERR_INVALID_STATE is returned.
 *
 * @param port    Port descriptor holding the SDA/SCL pins.
 * @param out_bus Out: I2C master bus handle.
 * @note Each successful call must be paired with one rootmaker_del_i2c_bus()
 *       call. The underlying bus is deleted after the final release.
 * @note Bus acquisition and release are not thread-safe. Call them serially
 *       from the application lifecycle manager.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, or an error from i2c_new_master_bus().
 */
esp_err_t rootmaker_new_i2c_bus(rootmaker_port_t *port, i2c_master_bus_handle_t *out_bus);

/**
 * Release one reference to an I2C bus previously acquired by
 * rootmaker_new_i2c_bus(). The bus is deleted only when the final reference is
 * released. The bus is located by matching the port's SDA/SCL pins, so the
 * caller only passes the same port used at acquisition time.
 *
 * @param port  Port whose pins identify the bus.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NOT_FOUND, or an error from i2c_del_master_bus().
 */
esp_err_t rootmaker_del_i2c_bus(rootmaker_port_t *port);

#ifdef __cplusplus
}
#endif
