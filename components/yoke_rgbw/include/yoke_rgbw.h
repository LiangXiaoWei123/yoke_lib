/**
 * @file yoke_rgbw.h
 * @brief RGBW WS2812 灯带组件。
 */

#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t gpio_num; /**< 灯带数据引脚。 */
    uint16_t led_num;    /**< 灯珠数量，必须大于 0。 */
} yoke_rgbw_config_t;

/** 返回空配置；调用前必须指定 gpio_num 和 led_num。 */
yoke_rgbw_config_t yoke_rgbw_default_config(void);

/** 初始化一个 RGBW（GRBW 字节顺序）WS2812 灯带。 */
esp_err_t yoke_rgbw_init(const yoke_rgbw_config_t *config);

/** 释放 RMT 和灯带缓冲区。 */
esp_err_t yoke_rgbw_deinit(void);

/** 设置全部灯珠为同一个 RGBW 颜色并立即刷新。 */
esp_err_t yoke_rgbw_set_all(uint8_t red, uint8_t green, uint8_t blue, uint8_t white);

#ifdef __cplusplus
}
#endif
