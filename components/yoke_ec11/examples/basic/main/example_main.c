/**
 * @file example_main.c
 * @brief Yoke-EC11-V10 两灯颜色调节示例。
 */

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

#include "yoke_ec11.h"

#define EXAMPLE_I2C_PORT I2C_NUM_0
#define EXAMPLE_I2C_SDA  GPIO_NUM_15
#define EXAMPLE_I2C_SCL  GPIO_NUM_16

static const char *TAG = "yoke_ec11_example";

static esp_err_t example_create_i2c_bus(i2c_master_bus_handle_t *bus)
{
    const i2c_master_bus_config_t config = {
        .i2c_port = EXAMPLE_I2C_PORT,
        .sda_io_num = EXAMPLE_I2C_SDA,
        .scl_io_num = EXAMPLE_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&config, bus);
}

static uint16_t wrap_hue(int hue)
{
    hue %= 360;
    return (uint16_t)(hue < 0 ? hue + 360 : hue);
}

static uint8_t clamp_brightness_percent(int value)
{
    if (value < 0) return 0U;
    if (value > 100) return 100U;
    return (uint8_t)value;
}

static yoke_ec11_rgb_t hue_to_rgb(uint16_t hue)
{
    uint8_t sector = (uint8_t)(hue / 60U);
    uint8_t level = (uint8_t)(((hue % 60U) * 255U) / 60U);

    switch (sector) {
    case 0: return (yoke_ec11_rgb_t){255U, level, 0U};
    case 1: return (yoke_ec11_rgb_t){(uint8_t)(255U - level), 255U, 0U};
    case 2: return (yoke_ec11_rgb_t){0U, 255U, level};
    case 3: return (yoke_ec11_rgb_t){0U, (uint8_t)(255U - level), 255U};
    case 4: return (yoke_ec11_rgb_t){level, 0U, 255U};
    default: return (yoke_ec11_rgb_t){255U, 0U, (uint8_t)(255U - level)};
    }
}

static uint16_t rgb_to_hue(yoke_ec11_rgb_t color)
{
    uint8_t max = color.red;
    uint8_t min = color.red;
    if (color.green > max) max = color.green;
    if (color.blue > max) max = color.blue;
    if (color.green < min) min = color.green;
    if (color.blue < min) min = color.blue;

    int delta = (int)max - min;
    if (delta == 0) return 0U;

    int hue;
    if (max == color.red) {
        hue = 60 * ((int)color.green - color.blue) / delta;
    } else if (max == color.green) {
        hue = 120 + 60 * ((int)color.blue - color.red) / delta;
    } else {
        hue = 240 + 60 * ((int)color.red - color.green) / delta;
    }
    return wrap_hue(hue);
}

static yoke_ec11_rgb_t rotate_color(yoke_ec11_rgb_t color, int16_t diff)
{
    return hue_to_rgb(wrap_hue((int)rgb_to_hue(color) + diff * 5));
}

static yoke_ec11_rgb_t scale_color(yoke_ec11_rgb_t color, uint8_t brightness_percent)
{
    color.red = (uint8_t)(((uint16_t)color.red * brightness_percent) / 100U);
    color.green = (uint8_t)(((uint16_t)color.green * brightness_percent) / 100U);
    color.blue = (uint8_t)(((uint16_t)color.blue * brightness_percent) / 100U);
    return color;
}

static esp_err_t update_leds(const yoke_ec11_t *yoke, const yoke_ec11_rgb_t colors[2],
                             uint8_t brightness_percent)
{
    return yoke_ec11_set_ws2812_all(
        yoke, scale_color(colors[0], brightness_percent),
        scale_color(colors[1], brightness_percent));
}

void app_main(void)
{
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(example_create_i2c_bus(&i2c_bus));

    yoke_ec11_t yoke;
    yoke_ec11_config_t config = yoke_ec11_default_config();
    config.verify_device_id = true;
    ESP_ERROR_CHECK(yoke_ec11_init(&yoke, i2c_bus, &config));

    uint8_t selected_led = 0U;
    yoke_ec11_rgb_t colors[2] = {
        {.red = 255U, .green = 0U, .blue = 0U},
        {.red = 0U, .green = 0U, .blue = 255U},
    };
    uint8_t brightness_percent = 10U;
    bool adjusted_brightness = false;

    ESP_ERROR_CHECK(yoke_ec11_set_ws2812_all(
        &yoke, scale_color(colors[0], brightness_percent),
        scale_color(colors[1], brightness_percent)));
    ESP_LOGI(TAG, "当前调节对象：LED1。旋转调颜色；按住再旋转调亮度；短按切换灯对象。");

    while (true) {
        yoke_ec11_key_t key;
        int16_t diff;

        ESP_ERROR_CHECK(yoke_ec11_read_key(&yoke, &key));
        ESP_ERROR_CHECK(yoke_ec11_read_encoder_diff(&yoke, &diff));

        if (diff != 0) {
            if (key.pressed) {
                brightness_percent = clamp_brightness_percent(
                    (int)brightness_percent + diff * 5);
                adjusted_brightness = true;
                ESP_ERROR_CHECK(update_leds(&yoke, colors, brightness_percent));
                ESP_LOGI(TAG, "灯光亮度：%u%%", brightness_percent);
            } else {
                colors[selected_led] = rotate_color(colors[selected_led], diff);
                ESP_ERROR_CHECK(update_leds(&yoke, colors, brightness_percent));
                yoke_ec11_rgb_t color = scale_color(colors[selected_led], brightness_percent);
                ESP_LOGI(TAG, "LED%u：RGB=(%u, %u, %u)", selected_led + 1U,
                         color.red, color.green, color.blue);
            }
        }

        if (key.press_count != 0U) {
            if (!adjusted_brightness) {
                selected_led = selected_led == 0U ? 1U : 0U;
                ESP_LOGI(TAG, "当前调节对象：LED%u", selected_led + 1U);
            }
            adjusted_brightness = false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
