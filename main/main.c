#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "yoke_ec11.h"
#include "yoke_keyw.h"
#include "yoke_moto.h"
#include "yoke_rad60.h"
#include "yoke_rgbw.h"

const char *TAG = "main";

#define RADAR_UART_NUM  UART_NUM_1
#define RADAR_TX_GPIO   GPIO_NUM_19
#define RADAR_RX_GPIO   GPIO_NUM_20
#define RADAR_BAUDRATE  921600U

#define EC11_I2C_PORT   I2C_NUM_0
#define EC11_SDA_GPIO   GPIO_NUM_13
#define EC11_SCL_GPIO   GPIO_NUM_14

#define MOTOR_PWMA_GPIO          GPIO_NUM_17
#define MOTOR_PWMB_GPIO          GPIO_NUM_18

// GPIO17 and GPIO18 are assigned to Yoke-KEYW. Do not initialize the motor
// while this temporary pin assignment is in use.

static bool s_radar_initialized;
static bool s_ec11_initialized;
static i2c_master_bus_handle_t s_ec11_i2c_bus;
static yoke_ec11_t s_ec11;
static yoke_moto_t s_motor;
static yoke_keyw_t s_keyw;

static void rgbw_set_all(uint8_t red, uint8_t green, uint8_t blue, uint8_t white)
{
    esp_err_t ret = yoke_rgbw_set_all(red, green, blue, white);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RGBW 设置失败: %s", esp_err_to_name(ret));
    }
}

static void print_help(void)
{
    ESP_LOGI(TAG, "RGBW: r=red, g=green, b=blue, w=white, o=off");
    ESP_LOGI(TAG, "RAD60: i=init, d=read, e=enable, x=disable, u=deinit, h=help");
    ESP_LOGI(TAG, "EC11: I=init, K=key, C=count, D=diff, R/G/B/W=color, O=off, U=deinit");
    ESP_LOGI(TAG, "MOTOR: m=init, f=forward, v=reverse, s=coast, q=brake, 1/2/3/4=speed, z=deinit");
    ESP_LOGI(TAG, "Yoke-KEYW: GPIO17 button, GPIO18 PWM LED");
}

static void keyw_event_callback(yoke_keyw_event_t event, void *user_data)
{
    (void)user_data;
    ESP_LOGI(TAG, "Yoke-KEYW event: %s", yoke_keyw_event_to_string(event));
}

static bool motor_is_ready(void)
{
    if (!s_motor.initialized) {
        ESP_LOGW(TAG, "Motor is not initialized; input m first");
        return false;
    }
    return true;
}

static void motor_set_speed(uint8_t percent)
{
    if (!motor_is_ready()) return;

    esp_err_t ret = yoke_moto_set_speed_percent(&s_motor, percent);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Motor speed=%u%%", percent);
    } else {
        ESP_LOGE(TAG, "Motor speed set failed: %s", esp_err_to_name(ret));
    }
}

static void motor_init(void)
{
    if (s_motor.initialized) {
        ESP_LOGW(TAG, "Motor is already initialized");
        return;
    }

    yoke_moto_config_t config = yoke_moto_default_config();
    config.pwma_gpio_num = MOTOR_PWMA_GPIO;
    config.pwmb_gpio_num = MOTOR_PWMB_GPIO;
    esp_err_t ret = yoke_moto_init(&s_motor, &config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Motor initialized: PWMA=%d PWMB=%d, speed=50%%, coast", MOTOR_PWMA_GPIO,
                 MOTOR_PWMB_GPIO);
        return;
    }

    ESP_LOGE(TAG, "Motor init failed: %s", esp_err_to_name(ret));
    (void)yoke_moto_deinit(&s_motor);
}

static void motor_drive(bool forward)
{
    if (!motor_is_ready()) return;

    esp_err_t ret = forward ? yoke_moto_forward(&s_motor) : yoke_moto_reverse(&s_motor);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Motor %s", forward ? "forward" : "reverse");
    } else {
        ESP_LOGE(TAG, "Motor direction set failed: %s", esp_err_to_name(ret));
    }
}

static void motor_stop(bool brake)
{
    if (!motor_is_ready()) return;

    esp_err_t ret = brake ? yoke_moto_brake(&s_motor) : yoke_moto_coast(&s_motor);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Motor %s", brake ? "brake" : "coast");
    } else {
        ESP_LOGE(TAG, "Motor stop failed: %s", esp_err_to_name(ret));
    }
}

static void motor_deinit(void)
{
    if (!motor_is_ready()) return;

    esp_err_t ret = yoke_moto_deinit(&s_motor);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Motor deinitialized");
    } else {
        ESP_LOGE(TAG, "Motor deinit failed: %s", esp_err_to_name(ret));
    }
}

static bool ec11_is_ready(void)
{
    if (!s_ec11_initialized) {
        ESP_LOGW(TAG, "EC11 is not initialized; input I first");
        return false;
    }
    return true;
}

static void ec11_init(void)
{
    if (s_ec11_initialized) {
        ESP_LOGW(TAG, "EC11 is already initialized");
        return;
    }

    if (s_ec11_i2c_bus == NULL) {
        const i2c_master_bus_config_t bus_config = {
            .i2c_port = EC11_I2C_PORT,
            .sda_io_num = EC11_SDA_GPIO,
            .scl_io_num = EC11_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        esp_err_t ret = i2c_new_master_bus(&bus_config, &s_ec11_i2c_bus);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "EC11 I2C bus init failed: %s", esp_err_to_name(ret));
            return;
        }
    }

    yoke_ec11_config_t config = yoke_ec11_default_config();
    config.verify_device_id = true;
    esp_err_t ret = yoke_ec11_init(&s_ec11, s_ec11_i2c_bus, &config);
    if (ret == ESP_OK) {
        s_ec11_initialized = true;
        ESP_LOGI(TAG, "EC11 initialized: I2C%d SDA=%d SCL=%d", EC11_I2C_PORT,
                 EC11_SDA_GPIO, EC11_SCL_GPIO);
    } else {
        ESP_LOGE(TAG, "EC11 init failed: %s", esp_err_to_name(ret));
    }
}

static void ec11_read_key(void)
{
    if (!ec11_is_ready()) return;

    yoke_ec11_key_t key;
    esp_err_t ret = yoke_ec11_read_key(&s_ec11, &key);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "EC11 key: pressed=%d press_count=%u", key.pressed, key.press_count);
    } else {
        ESP_LOGE(TAG, "EC11 key read failed: %s", esp_err_to_name(ret));
    }
}

static void ec11_read_encoder(bool read_diff)
{
    if (!ec11_is_ready()) return;

    int16_t value;
    esp_err_t ret = read_diff ? yoke_ec11_read_encoder_diff(&s_ec11, &value)
                              : yoke_ec11_read_encoder_count(&s_ec11, &value);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "EC11 %s=%d", read_diff ? "diff" : "count", value);
    } else {
        ESP_LOGE(TAG, "EC11 %s read failed: %s", read_diff ? "diff" : "count",
                 esp_err_to_name(ret));
    }
}

static void ec11_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!ec11_is_ready()) return;

    const yoke_ec11_rgb_t color = {.red = red, .green = green, .blue = blue};
    esp_err_t ret = yoke_ec11_set_ws2812_all(&s_ec11, color, color);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EC11 LED set failed: %s", esp_err_to_name(ret));
    }
}

static void ec11_deinit(void)
{
    if (!ec11_is_ready()) return;

    esp_err_t ret = yoke_ec11_deinit(&s_ec11);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EC11 deinit failed: %s", esp_err_to_name(ret));
        return;
    }
    s_ec11_initialized = false;

    ret = i2c_del_master_bus(s_ec11_i2c_bus);
    if (ret == ESP_OK) {
        s_ec11_i2c_bus = NULL;
        ESP_LOGI(TAG, "EC11 deinitialized");
    } else {
        ESP_LOGE(TAG, "EC11 I2C bus deinit failed: %s", esp_err_to_name(ret));
    }
}

static void radar_init(void)
{
    if (s_radar_initialized) {
        ESP_LOGW(TAG, "RAD60 is already initialized");
        return;
    }

    esp_err_t ret = yoke_rad60_init(RADAR_UART_NUM, RADAR_TX_GPIO, RADAR_RX_GPIO,
                                    RADAR_BAUDRATE);
    if (ret == ESP_OK) {
        s_radar_initialized = true;
        ESP_LOGI(TAG, "RAD60 initialized: UART%d TX=%d RX=%d baud=%lu", RADAR_UART_NUM,
                 RADAR_TX_GPIO, RADAR_RX_GPIO, (unsigned long)RADAR_BAUDRATE);
    } else {
        ESP_LOGE(TAG, "RAD60 init failed: %s", esp_err_to_name(ret));
    }
}

static void radar_read(void)
{
    if (!s_radar_initialized) {
        ESP_LOGW(TAG, "RAD60 is not initialized; input i first");
        return;
    }

    yoke_rad60_radar_data_t data;
    esp_err_t ret = yoke_rad60_get_radar_data(&data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RAD60 read failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "RAD60: comm=%d detected=%d objects=%u", data.is_comm_ok,
             data.is_detected, data.radar_info.obj_num);
    uint8_t object_count = data.radar_info.obj_num > 2U ? 2U : data.radar_info.obj_num;
    for (uint8_t index = 0; index < object_count; ++index) {
        const multitarget_obj_info_t *object = &data.radar_info.obj[index];
        ESP_LOGI(TAG, "  object[%u]: distance=%u cm angle=%d deg velocity=%d cm/s id=%u",
                 index, object->range_val, object->angle_val, object->velo_val, object->objid);
    }
}

static void radar_set_enabled(bool enabled)
{
    if (!s_radar_initialized) {
        ESP_LOGW(TAG, "RAD60 is not initialized; input i first");
        return;
    }

    esp_err_t ret = yoke_rad60_set_radar_enable(enabled ? 1U : 0U);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "RAD60 detection %s", enabled ? "enabled" : "disabled");
    } else {
        ESP_LOGE(TAG, "RAD60 enable command failed: %s", esp_err_to_name(ret));
    }
}

static void radar_deinit(void)
{
    if (!s_radar_initialized) {
        ESP_LOGW(TAG, "RAD60 is not initialized");
        return;
    }

    esp_err_t ret = yoke_rad60_deinit();
    if (ret == ESP_OK) {
        s_radar_initialized = false;
        ESP_LOGI(TAG, "RAD60 deinitialized");
    } else {
        ESP_LOGE(TAG, "RAD60 deinit failed: %s", esp_err_to_name(ret));
    }
}

void app_main(void)
{
    yoke_rgbw_config_t rgbw_config = yoke_rgbw_default_config();
    rgbw_config.gpio_num = 15;
    rgbw_config.led_num = 3;
    ESP_ERROR_CHECK(yoke_rgbw_init(&rgbw_config));
    rgbw_set_all(0, 0, 0, 0);
    yoke_keyw_config_t keyw_config = yoke_keyw_default_config();
    keyw_config.event_cb = keyw_event_callback;
    ESP_ERROR_CHECK(yoke_keyw_init(&s_keyw, &keyw_config));
    print_help();

    char input;
    while(1){
        vTaskDelay(pdMS_TO_TICKS(10));
        if (scanf("%c", &input) == 1){
            if (input == '\r' || input == '\n') {
                continue;
            }
            ESP_LOGI(TAG, "input: %c", input);
            if(input == 'r'){
                ESP_LOGI(TAG, "set all to red");
                rgbw_set_all(25, 0, 0, 0);
            } else if(input == 'g'){
                ESP_LOGI(TAG, "set all to green");
                rgbw_set_all(0, 25, 0, 0);
            } else if(input == 'b'){
                ESP_LOGI(TAG, "set all to blue");
                rgbw_set_all(0, 0, 25, 0);
            } else if(input == 'w'){
                ESP_LOGI(TAG, "set all to white");
                rgbw_set_all(0, 0, 0, 25);
            } else if(input == 'o'){
                ESP_LOGI(TAG, "turn off all");
                rgbw_set_all(0, 0, 0, 0);
            } else if(input == 'h'){
                print_help();
            } else if(input == 'i'){
                radar_init();
            } else if(input == 'd'){
                radar_read();
            } else if(input == 'e'){
                radar_set_enabled(true);
            } else if(input == 'x'){
                radar_set_enabled(false);
            } else if(input == 'u'){
                radar_deinit();
            } else if(input == 'I'){
                ec11_init();
            } else if(input == 'K'){
                ec11_read_key();
            } else if(input == 'C'){
                ec11_read_encoder(false);
            } else if(input == 'D'){
                ec11_read_encoder(true);
            } else if(input == 'R'){
                ec11_set_color(25, 0, 0);
            } else if(input == 'G'){
                ec11_set_color(0, 25, 0);
            } else if(input == 'B'){
                ec11_set_color(0, 0, 25);
            } else if(input == 'W'){
                ec11_set_color(25, 25, 25);
            } else if(input == 'O'){
                ec11_set_color(0, 0, 0);
            } else if(input == 'U'){
                ec11_deinit();
            } else if(input == 'm'){
                motor_init();
            } else if(input == 'f'){
                motor_drive(true);
            } else if(input == 'v'){
                motor_drive(false);
            } else if(input == 's'){
                motor_stop(false);
            } else if(input == 'q'){
                motor_stop(true);
            } else if(input == '1'){
                motor_set_speed(25);
            } else if(input == '2'){
                motor_set_speed(50);
            } else if(input == '3'){
                motor_set_speed(75);
            } else if(input == '4'){
                motor_set_speed(100);
            } else if(input == 'z'){
                motor_deinit();
            } else {
                ESP_LOGW(TAG, "unknown command: %c", input);
            }
        }
    }
}
