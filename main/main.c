/**
 * Yoke-EC11-V10 两灯颜色调节演示
 *
 * 按键：在 LED1 和 LED2 之间切换当前调节对象。
 * 旋转：调整当前选中 WS2812 的颜色。
 */
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "yoke_ec11.h"

static const char *TAG = "yoke_demo";
static i2c_master_bus_handle_t s_i2c_bus;

/* 创建应用持有的 I2C 总线；其他 I2C 外设也应使用 s_i2c_bus。 */
static esp_err_t init_i2c_bus(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_15,
        .scl_io_num = GPIO_NUM_16,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_config, &s_i2c_bus);
}

/* 将角度限制在 0～359°。 */
static uint16_t wrap_hue(int hue)
{
    hue %= 360;
    if (hue < 0) hue += 360;
    return (uint16_t)hue;
}

/* 将亮度百分比限制在 0～100% 范围。 */
static uint8_t clamp_brightness_percent(int value)
{
    if (value < 0) return 0U;
    if (value > 100) return 100U;
    return (uint8_t)value;
}

/* 根据 0～359° 的色相生成全亮度 RGB 彩虹色。 */
static yoke_ec11_rgb_t hue_to_rgb(uint16_t hue)
{
    uint8_t sector = (uint8_t)(hue / 60U);
    uint8_t level = (uint8_t)(((hue % 60U) * 255U) / 60U);

    switch (sector) {
    case 0:  return (yoke_ec11_rgb_t){.red = 255U, .green = level, .blue = 0U};
    case 1:  return (yoke_ec11_rgb_t){.red = (uint8_t)(255U - level), .green = 255U, .blue = 0U};
    case 2:  return (yoke_ec11_rgb_t){.red = 0U, .green = 255U, .blue = level};
    case 3:  return (yoke_ec11_rgb_t){.red = 0U, .green = (uint8_t)(255U - level), .blue = 255U};
    case 4:  return (yoke_ec11_rgb_t){.red = level, .green = 0U, .blue = 255U};
    default: return (yoke_ec11_rgb_t){.red = 255U, .green = 0U, .blue = (uint8_t)(255U - level)};
    }
}

/* 将 RGB 颜色换算为 0～359° 色相，仅供颜色旋转时临时使用。 */
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

/* 将 RGB 颜色沿彩虹色环旋转指定的编码器增量。 */
static yoke_ec11_rgb_t rotate_color(yoke_ec11_rgb_t color, int16_t diff)
{
    uint16_t hue = rgb_to_hue(color);
    hue = wrap_hue((int)hue + diff * 5);
    return hue_to_rgb(hue);
}

/* 按共享亮度百分比缩放颜色，亮度范围为 0～100%。 */
static yoke_ec11_rgb_t scale_color(yoke_ec11_rgb_t color, uint8_t brightness_percent)
{
    color.red = (uint8_t)(((uint16_t)color.red * brightness_percent) / 100U);
    color.green = (uint8_t)(((uint16_t)color.green * brightness_percent) / 100U);
    color.blue = (uint8_t)(((uint16_t)color.blue * brightness_percent) / 100U);
    return color;
}

/* 用各自的 RGB 颜色和共同亮度百分比更新两颗 WS2812。 */
static esp_err_t update_leds(const yoke_ec11_t *yoke, const yoke_ec11_rgb_t colors[2],
                             uint8_t brightness_percent)
{
    yoke_ec11_rgb_t led1 = scale_color(colors[0], brightness_percent);
    yoke_ec11_rgb_t led2 = scale_color(colors[1], brightness_percent);
    return yoke_ec11_set_ws2812_all(yoke, led1, led2);
}

void app_main(void)
{
    /* 第一步：应用创建 I2C 总线。 */
    esp_err_t ret = init_i2c_bus();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 总线创建失败：%s", esp_err_to_name(ret));
        return;
    }

    /* 第二步：将 Yoke 外设挂载到共享 I2C 总线。 */
    yoke_ec11_t yoke;
    yoke_ec11_config_t config = yoke_ec11_default_config();
    config.scl_speed_hz = 100000U;
    config.timeout_ms = 1000U;
    config.verify_device_id = true;
    ret = yoke_ec11_init(&yoke, s_i2c_bus, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Yoke 初始化失败：%s", esp_err_to_name(ret));
        return;
    }

    uint8_t selected_led = 0U;        /* 0 对应 LED1，1 对应 LED2。 */
    yoke_ec11_rgb_t colors[2] = {     /* 两颗灯各自保存当前 RGB 颜色。 */
        {.red = 255U, .green = 0U, .blue = 0U},
        {.red = 0U, .green = 0U, .blue = 255U},
    };
    uint8_t brightness_percent = 10U; /* 两颗灯共用 10% 初始亮度。 */
    bool adjusted_brightness = false; /* 用于区分短按与按住旋转。 */
    ESP_ERROR_CHECK(update_leds(&yoke, colors, brightness_percent));
    ESP_LOGI(TAG, "当前调节对象：LED1。旋转调颜色；按住再旋转调灯光亮度；短按切换灯对象。");

    while (true) {
        yoke_ec11_key_t key;
        ret = yoke_ec11_read_key(&yoke, &key);
        bool key_pressed = ret == ESP_OK && key.pressed;

        int16_t diff;
        esp_err_t diff_ret = yoke_ec11_read_encoder_diff(&yoke, &diff);
        if (diff_ret == ESP_OK && diff != 0) {
            if (key_pressed) {
                brightness_percent = clamp_brightness_percent(
                    (int)brightness_percent + diff * 5);
                adjusted_brightness = true;
                if (update_leds(&yoke, colors, brightness_percent) == ESP_OK) {
                    ESP_LOGI(TAG, "灯光亮度：%u%%", brightness_percent);
                }
            } else {
                colors[selected_led] = rotate_color(colors[selected_led], diff);
                yoke_ec11_rgb_t color = scale_color(colors[selected_led], brightness_percent);
                if (update_leds(&yoke, colors, brightness_percent) == ESP_OK) {
                    ESP_LOGI(TAG, "LED%u：RGB=(%u, %u, %u)", selected_led + 1U,
                             color.red, color.green, color.blue);
                }
            }
        }

        if (ret == ESP_OK && key.press_count != 0U) {
            if (!adjusted_brightness) {
                selected_led = selected_led == 0U ? 1U : 0U;
                ESP_LOGI(TAG, "当前调节对象：LED%u", selected_led + 1U);
            }
            adjusted_brightness = false;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
