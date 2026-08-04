/** @file yoke_moto.c */
#include "yoke_moto.h"

#include <string.h>

#include "bdc_motor.h"

static bdc_motor_handle_t yoke_moto_handle(const yoke_moto_t *moto)
{
    return (bdc_motor_handle_t)moto->handle;
}

static esp_err_t yoke_moto_check(const yoke_moto_t *moto)
{
    if (moto == NULL || !moto->initialized || moto->handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

yoke_moto_config_t yoke_moto_default_config(void)
{
    return (yoke_moto_config_t){
        .pwma_gpio_num = GPIO_NUM_NC,
        .pwmb_gpio_num = GPIO_NUM_NC,
        .pwm_freq_hz = 20000U,
        .resolution_hz = 10000000U,
        .mcpwm_group_id = 0,
        .default_speed_percent = 50U,
    };
}

esp_err_t yoke_moto_init(yoke_moto_t *moto, const yoke_moto_config_t *config)
{
    if (moto == NULL || config == NULL || config->pwma_gpio_num == GPIO_NUM_NC ||
        config->pwmb_gpio_num == GPIO_NUM_NC || config->pwma_gpio_num == config->pwmb_gpio_num ||
        config->pwm_freq_hz == 0U || config->resolution_hz < config->pwm_freq_hz ||
        config->default_speed_percent > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (moto->initialized || moto->handle != NULL) return ESP_ERR_INVALID_STATE;

    memset(moto, 0, sizeof(*moto));
    const bdc_motor_config_t motor_config = {
        .pwma_gpio_num = config->pwma_gpio_num,
        .pwmb_gpio_num = config->pwmb_gpio_num,
        .pwm_freq_hz = config->pwm_freq_hz,
    };
    const bdc_motor_mcpwm_config_t mcpwm_config = {
        .group_id = config->mcpwm_group_id,
        .resolution_hz = config->resolution_hz,
    };

    bdc_motor_handle_t handle = NULL;
    esp_err_t ret = bdc_motor_new_mcpwm_device(&motor_config, &mcpwm_config, &handle);
    if (ret != ESP_OK) return ret;

    moto->handle = handle;
    moto->period_ticks = config->resolution_hz / config->pwm_freq_hz;
    moto->initialized = true;
    ret = bdc_motor_enable(handle);
    if (ret == ESP_OK) ret = yoke_moto_set_speed_percent(moto, config->default_speed_percent);
    if (ret == ESP_OK) ret = bdc_motor_coast(handle);
    if (ret == ESP_OK) return ESP_OK;

    (void)yoke_moto_deinit(moto);
    return ret;
}

esp_err_t yoke_moto_deinit(yoke_moto_t *moto)
{
    esp_err_t ret = yoke_moto_check(moto);
    if (ret != ESP_OK) return ret;

    bdc_motor_handle_t handle = yoke_moto_handle(moto);
    esp_err_t first_error = bdc_motor_coast(handle);
    ret = bdc_motor_disable(handle);
    if (first_error == ESP_OK && ret != ESP_OK) first_error = ret;
    ret = bdc_motor_del(handle);
    if (first_error == ESP_OK && ret != ESP_OK) first_error = ret;
    if (ret == ESP_OK) memset(moto, 0, sizeof(*moto));
    return first_error;
}

esp_err_t yoke_moto_set_speed_percent(yoke_moto_t *moto, uint8_t percent)
{
    esp_err_t ret = yoke_moto_check(moto);
    if (ret != ESP_OK) return ret;
    if (percent > 100U) return ESP_ERR_INVALID_ARG;
    uint32_t compare_ticks = (moto->period_ticks * percent) / 100U;
    return bdc_motor_set_speed(yoke_moto_handle(moto), compare_ticks);
}

esp_err_t yoke_moto_forward(yoke_moto_t *moto)
{
    esp_err_t ret = yoke_moto_check(moto);
    return ret == ESP_OK ? bdc_motor_forward(yoke_moto_handle(moto)) : ret;
}

esp_err_t yoke_moto_reverse(yoke_moto_t *moto)
{
    esp_err_t ret = yoke_moto_check(moto);
    return ret == ESP_OK ? bdc_motor_reverse(yoke_moto_handle(moto)) : ret;
}

esp_err_t yoke_moto_coast(yoke_moto_t *moto)
{
    esp_err_t ret = yoke_moto_check(moto);
    return ret == ESP_OK ? bdc_motor_coast(yoke_moto_handle(moto)) : ret;
}

esp_err_t yoke_moto_brake(yoke_moto_t *moto)
{
    esp_err_t ret = yoke_moto_check(moto);
    return ret == ESP_OK ? bdc_motor_brake(yoke_moto_handle(moto)) : ret;
}

