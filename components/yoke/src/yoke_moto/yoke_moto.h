/**
 * @file yoke_moto.h
 * @brief Yoke-Moto brushed DC motor driver.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t pwma_gpio_num;      /**< H-bridge PWMA input. */
    gpio_num_t pwmb_gpio_num;      /**< H-bridge PWMB input. */
    uint32_t pwm_freq_hz;          /**< PWM frequency. */
    uint32_t resolution_hz;        /**< MCPWM timer resolution. */
    int mcpwm_group_id;            /**< MCPWM group. */
    uint8_t default_speed_percent; /**< Initial speed from 0 to 100 percent. */
} yoke_moto_config_t;

/** Driver instance; fields are implementation state and must not be modified by applications. */
typedef struct {
    void *handle;
    uint32_t period_ticks;
    bool initialized;
} yoke_moto_t;

/** Return the default configuration: 20 kHz, 10 MHz resolution, and 50% initial speed. */
yoke_moto_config_t yoke_moto_default_config(void);

/** Create and enable the motor, leaving it in the coast stop state. */
esp_err_t yoke_moto_init(yoke_moto_t *moto, const yoke_moto_config_t *config);

/** Release motor resources after coasting and disabling MCPWM. */
esp_err_t yoke_moto_deinit(yoke_moto_t *moto);

/** Set the speed from 0 to 100 percent. */
esp_err_t yoke_moto_set_speed_percent(yoke_moto_t *moto, uint8_t percent);

/** Drive forward. */
esp_err_t yoke_moto_forward(yoke_moto_t *moto);

/** Drive in reverse. */
esp_err_t yoke_moto_reverse(yoke_moto_t *moto);

/** Stop by coasting (fast decay). */
esp_err_t yoke_moto_coast(yoke_moto_t *moto);

/** Stop by braking (slow decay). */
esp_err_t yoke_moto_brake(yoke_moto_t *moto);

#ifdef __cplusplus
}
#endif
