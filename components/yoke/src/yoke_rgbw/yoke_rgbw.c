/** @file yoke_rgbw.c */
#include "yoke_rgbw.h"

#include <stdlib.h>
#include <string.h>

#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

#define YOKE_RGBW_RMT_RESOLUTION_HZ 10000000U

static const char *TAG = "yoke_rgbw";
static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static uint8_t *s_pixels;
static size_t s_pixels_size;

static const rmt_symbol_word_t s_zero = {
    .level0 = 1,
    .duration0 = 3,
    .level1 = 0,
    .duration1 = 9,
};
static const rmt_symbol_word_t s_one = {
    .level0 = 1,
    .duration0 = 9,
    .level1 = 0,
    .duration1 = 3,
};
static const rmt_symbol_word_t s_reset = {
    .level0 = 1,
    .duration0 = 250,
    .level1 = 0,
    .duration1 = 250,
};

static size_t yoke_rgbw_encode(const void *data, size_t data_size, size_t symbols_written,
                                size_t symbols_free, rmt_symbol_word_t *symbols, bool *done,
                                void *arg)
{
    (void)arg;
    if (symbols_free < 8) return 0;

    size_t byte_index = symbols_written / 8;
    const uint8_t *bytes = data;
    if (byte_index >= data_size) {
        symbols[0] = s_reset;
        *done = true;
        return 1;
    }

    for (uint8_t mask = 0x80U, index = 0; mask != 0U; mask >>= 1U) {
        symbols[index++] = (bytes[byte_index] & mask) ? s_one : s_zero;
    }
    return 8;
}

yoke_rgbw_config_t yoke_rgbw_default_config(void)
{
    return (yoke_rgbw_config_t){
        .gpio_num = GPIO_NUM_NC,
        .led_num = 0,
    };
}

esp_err_t yoke_rgbw_init(const yoke_rgbw_config_t *config)
{
    if (config == NULL || config->gpio_num == GPIO_NUM_NC || config->led_num == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_channel != NULL || s_encoder != NULL || s_pixels != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t pixels_size = (size_t)config->led_num * 4U;
    s_pixels = calloc(pixels_size, sizeof(*s_pixels));
    if (s_pixels == NULL) return ESP_ERR_NO_MEM;

    rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = config->gpio_num,
        .mem_block_symbols = 64,
        .resolution_hz = YOKE_RGBW_RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    esp_err_t ret = rmt_new_tx_channel(&channel_config, &s_channel);
    if (ret != ESP_OK) goto fail;

    rmt_simple_encoder_config_t encoder_config = {.callback = yoke_rgbw_encode};
    ret = rmt_new_simple_encoder(&encoder_config, &s_encoder);
    if (ret != ESP_OK) goto fail;

    ret = rmt_enable(s_channel);
    if (ret != ESP_OK) goto fail;
    s_pixels_size = pixels_size;
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "初始化失败：%s", esp_err_to_name(ret));
    (void)yoke_rgbw_deinit();
    return ret;
}

esp_err_t yoke_rgbw_deinit(void)
{
    esp_err_t ret = ESP_OK;
    if (s_channel != NULL) {
        esp_err_t err = rmt_disable(s_channel);
        if (err != ESP_OK && ret == ESP_OK) ret = err;
        err = rmt_del_channel(s_channel);
        if (err != ESP_OK && ret == ESP_OK) ret = err;
        s_channel = NULL;
    }
    if (s_encoder != NULL) {
        esp_err_t err = rmt_del_encoder(s_encoder);
        if (err != ESP_OK && ret == ESP_OK) ret = err;
        s_encoder = NULL;
    }
    free(s_pixels);
    s_pixels = NULL;
    s_pixels_size = 0;
    return ret;
}

esp_err_t yoke_rgbw_set_all(uint8_t red, uint8_t green, uint8_t blue, uint8_t white)
{
    if (s_channel == NULL || s_encoder == NULL || s_pixels == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t index = 0; index < s_pixels_size; index += 4U) {
        s_pixels[index] = green;
        s_pixels[index + 1U] = red;
        s_pixels[index + 2U] = blue;
        s_pixels[index + 3U] = white;
    }
    const rmt_transmit_config_t config = {.loop_count = 0};
    esp_err_t ret = rmt_transmit(s_channel, s_encoder, s_pixels, s_pixels_size, &config);
    return ret == ESP_OK ? rmt_tx_wait_all_done(s_channel, portMAX_DELAY) : ret;
}

