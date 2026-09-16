/**
 * @file yoke_motor.h
 * @brief Yoke-motor brushed DC motor driver.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "rootmaker_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque driver instance. Created by yoke_motor_init(); NULL on failure. */
typedef struct yoke_motor *yoke_motor_handle_t;

/** Create and enable the motor on a port (pins[0]=PWMA, pins[1]=PWMB), leaving it coasting. 20 kHz / 10 MHz / group 0 / 50% initial speed. Returns handle, or
 * NULL on failure. */
yoke_motor_handle_t yoke_motor_init(rootmaker_port_t port);

/** Release motor resources after coasting and disabling MCPWM. */
esp_err_t yoke_motor_deinit(yoke_motor_handle_t motor);

/** Set the speed from 0 to 100 percent. */
esp_err_t yoke_motor_set_speed_percent(yoke_motor_handle_t motor, uint8_t percent);

/** Drive forward. */
esp_err_t yoke_motor_forward(yoke_motor_handle_t motor);

/** Drive in reverse. */
esp_err_t yoke_motor_reverse(yoke_motor_handle_t motor);

/** Stop by coasting (fast decay). */
esp_err_t yoke_motor_coast(yoke_motor_handle_t motor);

/** Stop by braking (slow decay). */
esp_err_t yoke_motor_brake(yoke_motor_handle_t motor);

#ifdef __cplusplus
}
#endif
