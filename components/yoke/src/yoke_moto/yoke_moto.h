/**
 * @file yoke_moto.h
 * @brief Yoke-Moto 直流电机组件。
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
    gpio_num_t pwma_gpio_num;      /**< H 桥 PWMA 输入。 */
    gpio_num_t pwmb_gpio_num;      /**< H 桥 PWMB 输入。 */
    uint32_t pwm_freq_hz;          /**< PWM 频率。 */
    uint32_t resolution_hz;        /**< MCPWM 计数器分辨率。 */
    int mcpwm_group_id;            /**< MCPWM group。 */
    uint8_t default_speed_percent; /**< 初始化后的默认速度，范围 0～100。 */
} yoke_moto_config_t;

/** 驱动实例；成员为实现状态，不应由应用直接修改。 */
typedef struct {
    void *handle;
    uint32_t period_ticks;
    bool initialized;
} yoke_moto_t;

/** 返回默认配置：20 kHz、10 MHz 分辨率、50% 默认速度。 */
yoke_moto_config_t yoke_moto_default_config(void);

/** 创建、启用电机，并使其处于 coast 停止状态。 */
esp_err_t yoke_moto_init(yoke_moto_t *moto, const yoke_moto_config_t *config);

/** 释放电机资源；释放前会先 coast 并停止 MCPWM。 */
esp_err_t yoke_moto_deinit(yoke_moto_t *moto);

/** 设置速度百分比，范围 0～100。 */
esp_err_t yoke_moto_set_speed_percent(yoke_moto_t *moto, uint8_t percent);

/** 正转。 */
esp_err_t yoke_moto_forward(yoke_moto_t *moto);

/** 反转。 */
esp_err_t yoke_moto_reverse(yoke_moto_t *moto);

/** coast 停止（快速衰减）。 */
esp_err_t yoke_moto_coast(yoke_moto_t *moto);

/** brake 停止（慢速衰减）。 */
esp_err_t yoke_moto_brake(yoke_moto_t *moto);

#ifdef __cplusplus
}
#endif

