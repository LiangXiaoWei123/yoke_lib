/*
 * Yoke-EC11 demo: attach the peripheral to an I2C bus, then periodically read
 * the encoder, button, and drive the two onboard WS2812 LEDs.
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "yoke_ec11.h"

#define I2C_PORT I2C_NUM_0
#define I2C_SDA_GPIO GPIO_NUM_15
#define I2C_SCL_GPIO GPIO_NUM_16

static const char *TAG = "ec11_demo";

void app_main(void) {
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    yoke_ec11_handle_t ec11;
    yoke_ec11_config_t config = YOKE_EC11_DEFAULT_CONFIG();
    config.verify_device_id = true;
    ESP_ERROR_CHECK(yoke_ec11_init(&ec11, bus_handle, &config));

    uint16_t device_id = 0;
    ESP_ERROR_CHECK(yoke_ec11_read_device_id(&ec11, &device_id));
    ESP_LOGI(TAG, "EC11 device ID: 0x%04X", device_id);

    ESP_LOGI(TAG, "Spinning a rainbow on the onboard LEDs...");
    const yoke_ec11_rgb_t palette[] = {
        { .red = 255, .green = 0,   .blue = 0   },
        { .red = 0,   .green = 255, .blue = 0   },
        { .red = 0,   .green = 0,   .blue = 255 },
    };
    uint8_t step = 0;

    int16_t last_count = 0;
    while (1) {
        int16_t count = 0;
        ESP_ERROR_CHECK(yoke_ec11_read_encoder_count(&ec11, &count));
        if (count != last_count) {
            ESP_LOGI(TAG, "Encoder count: %d", count);
            last_count = count;
        }

        yoke_ec11_key_t key;
        if (yoke_ec11_read_key(&ec11, &key) == ESP_OK && key.pressed) {
            ESP_LOGI(TAG, "Button pressed, press count: %u", key.press_count);
        }

        const yoke_ec11_rgb_t color = palette[step % 3];
        ESP_ERROR_CHECK(yoke_ec11_set_ws2812_all(&ec11, color, color));
        step = (step + 1) % 3;

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
