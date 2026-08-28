/** @file yoke_ec11.c */
#include "yoke_ec11.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#define TAG "yoke_ec11"

#define YOKE_REG_ID_HIGH 0x00U
#define YOKE_REG_KEY 0x02U
#define YOKE_REG_KEY_TIME 0x03U
#define YOKE_REG_COUNT_HIGH 0x04U
#define YOKE_REG_DIFF_HIGH 0x06U
#define YOKE_REG_LED1_RED 0x08U
#define YOKE_REG_CTRL1 0x0EU
#define YOKE_REG_CTRL2 0x0FU

#define YOKE_KEY_PRESSED (1U << 7)
#define YOKE_KEY_COUNT_MASK 0x0FU

struct yoke_ec11 {
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    rootmaker_port_t port;
    uint8_t device_address;
    uint32_t scl_speed_hz;
    uint32_t timeout_ms;
    bool initialized;
};

static bool yoke_is_address_valid(uint8_t address) {
    return address >= 0x08U && address <= 0x77U;
}

static esp_err_t yoke_check(yoke_ec11_handle_t yoke) {
    if (yoke == NULL || !yoke->initialized || yoke->dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t yoke_read(yoke_ec11_handle_t yoke, uint8_t reg, uint8_t *data, size_t len) {
    if (data == NULL || len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = yoke_check(yoke);
    if (ret != ESP_OK) {
        return ret;
    }
    return i2c_master_transmit_receive(yoke->dev_handle, &reg, 1U, data, len, yoke->timeout_ms);
}

static esp_err_t yoke_write(yoke_ec11_handle_t yoke, uint8_t reg, const uint8_t *data, size_t len) {
    if (data == NULL || len == 0U || len > 6U) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = yoke_check(yoke);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t tx[7];
    tx[0] = reg;
    memcpy(&tx[1], data, len);
    return i2c_master_transmit(yoke->dev_handle, tx, len + 1U, yoke->timeout_ms);
}

static esp_err_t yoke_add_device(yoke_ec11_handle_t yoke, uint8_t address) {
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = yoke->scl_speed_hz,
    };
    esp_err_t ret = i2c_master_bus_add_device(yoke->bus_handle, &device_config, &yoke->dev_handle);
    if (ret == ESP_OK) {
        yoke->device_address = address;
    }
    return ret;
}

yoke_ec11_handle_t yoke_ec11_init(rootmaker_port_t port) {
    yoke_ec11_handle_t yoke = calloc(1, sizeof(struct yoke_ec11));
    if (yoke == NULL) {
        ESP_LOGE(TAG, "no mem");
        return NULL;
    }
    yoke->port = port;
    yoke->device_address = YOKE_EC11_DEFAULT_I2C_ADDRESS;
    yoke->scl_speed_hz = YOKE_EC11_DEFAULT_I2C_SPEED_HZ;
    yoke->timeout_ms = YOKE_EC11_DEFAULT_TIMEOUT_MS;

    esp_err_t ret = rootmaker_use_port(&port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "port in use: %s", esp_err_to_name(ret));
        free(yoke);
        return NULL;
    }
    ret = rootmaker_new_i2c_bus(&port, &yoke->bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "new i2c bus failed: %s", esp_err_to_name(ret));
        (void)rootmaker_release_port(&port);
        free(yoke);
        return NULL;
    }
    ret = yoke_add_device(yoke, YOKE_EC11_DEFAULT_I2C_ADDRESS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(ret));
        (void)rootmaker_del_i2c_bus(&port);
        (void)rootmaker_release_port(&port);
        free(yoke);
        return NULL;
    }
    yoke->initialized = true;

    uint16_t device_id = 0;
    ret = yoke_ec11_read_device_id(yoke, &device_id);
    if (ret != ESP_OK || device_id != YOKE_EC11_DEVICE_ID) {
        ESP_LOGE(TAG, "device id check failed: 0x%04X (%s)", device_id, esp_err_to_name(ret));
        (void)yoke_ec11_deinit(yoke);
        return NULL;
    }
    return yoke;
}

esp_err_t yoke_ec11_deinit(yoke_ec11_handle_t yoke) {
    if (yoke == NULL || !yoke->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = i2c_master_bus_rm_device(yoke->dev_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = rootmaker_del_i2c_bus(&yoke->port);
    if (ret == ESP_OK) {
        (void)rootmaker_release_port(&yoke->port);
        free(yoke);
    }
    return ret;
}

esp_err_t yoke_ec11_probe(yoke_ec11_handle_t yoke) {
    esp_err_t ret = yoke_check(yoke);
    return ret == ESP_OK ? i2c_master_probe(yoke->bus_handle, yoke->device_address, yoke->timeout_ms) : ret;
}

esp_err_t yoke_ec11_read_device_id(yoke_ec11_handle_t yoke, uint16_t *device_id) {
    if (device_id == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data[2];
    esp_err_t ret = yoke_read(yoke, YOKE_REG_ID_HIGH, data, sizeof(data));
    if (ret == ESP_OK)
        *device_id = ((uint16_t)data[0] << 8) | data[1];
    return ret;
}

esp_err_t yoke_ec11_read_key(yoke_ec11_handle_t yoke, yoke_ec11_key_t *key) {
    if (key == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data;
    esp_err_t ret = yoke_read(yoke, YOKE_REG_KEY, &data, 1U);
    if (ret == ESP_OK) {
        key->pressed = (data & YOKE_KEY_PRESSED) != 0U;
        key->press_count = data & YOKE_KEY_COUNT_MASK;
    }
    return ret;
}

esp_err_t yoke_ec11_read_key_press_time_ms(yoke_ec11_handle_t yoke, uint16_t *time_ms) {
    if (time_ms == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data;
    esp_err_t ret = yoke_read(yoke, YOKE_REG_KEY_TIME, &data, 1U);
    if (ret == ESP_OK)
        *time_ms = (uint16_t)data * 100U;
    return ret;
}

esp_err_t yoke_ec11_read_encoder_count(yoke_ec11_handle_t yoke, int16_t *count) {
    if (count == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data[2];
    esp_err_t ret = yoke_read(yoke, YOKE_REG_COUNT_HIGH, data, sizeof(data));
    if (ret == ESP_OK)
        *count = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
    return ret;
}

esp_err_t yoke_ec11_read_encoder_diff(yoke_ec11_handle_t yoke, int16_t *diff) {
    if (diff == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data[2];
    esp_err_t ret = yoke_read(yoke, YOKE_REG_DIFF_HIGH, data, sizeof(data));
    if (ret == ESP_OK)
        *diff = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
    return ret;
}

esp_err_t yoke_ec11_set_ws2812(yoke_ec11_handle_t yoke, uint8_t index, yoke_ec11_rgb_t color) {
    if (index > 1U)
        return ESP_ERR_INVALID_ARG;
    uint8_t data[] = { color.red, color.green, color.blue };
    return yoke_write(yoke, (uint8_t)(YOKE_REG_LED1_RED + index * 3U), data, sizeof(data));
}

esp_err_t yoke_ec11_set_ws2812_all(yoke_ec11_handle_t yoke, yoke_ec11_rgb_t led1, yoke_ec11_rgb_t led2) {
    uint8_t data[] = { led1.red, led1.green, led1.blue, led2.red, led2.green, led2.blue };
    return yoke_write(yoke, YOKE_REG_LED1_RED, data, sizeof(data));
}

esp_err_t yoke_ec11_get_ws2812(yoke_ec11_handle_t yoke, uint8_t index, yoke_ec11_rgb_t *color) {
    if (index > 1U || color == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data[3];
    esp_err_t ret = yoke_read(yoke, (uint8_t)(YOKE_REG_LED1_RED + index * 3U), data, sizeof(data));
    if (ret == ESP_OK)
        *color = (yoke_ec11_rgb_t){ .red = data[0], .green = data[1], .blue = data[2] };
    return ret;
}

esp_err_t yoke_ec11_read_ctrl1(yoke_ec11_handle_t yoke, uint8_t *value) {
    return yoke_read(yoke, YOKE_REG_CTRL1, value, 1U);
}

esp_err_t yoke_ec11_write_ctrl1(yoke_ec11_handle_t yoke, uint8_t value) {
    return yoke_write(yoke, YOKE_REG_CTRL1, &value, 1U);
}

esp_err_t yoke_ec11_enter_test_mode(yoke_ec11_handle_t yoke) {
    return yoke_ec11_write_ctrl1(yoke, 0x5AU);
}

esp_err_t yoke_ec11_configure_slave_address(yoke_ec11_handle_t yoke, uint8_t address_low_3bits) {
    if (address_low_3bits > 0x07U)
        return ESP_ERR_INVALID_ARG;
    /* CTRL2 bit layout: 1 !A2 !A1 !A0 0 A2 A1 A0. */
    uint8_t ctrl2 = (uint8_t)(0x80U | (((~address_low_3bits) & 0x07U) << 4) | address_low_3bits);
    return yoke_write(yoke, YOKE_REG_CTRL2, &ctrl2, 1U);
}

esp_err_t yoke_ec11_set_active_address(yoke_ec11_handle_t yoke, uint8_t device_address) {
    if (yoke_check(yoke) != ESP_OK || !yoke_is_address_valid(device_address))
        return ESP_ERR_INVALID_ARG;
    if (device_address == yoke->device_address)
        return ESP_OK;

    esp_err_t ret = i2c_master_device_change_address(yoke->dev_handle, device_address, yoke->timeout_ms);
    if (ret == ESP_OK) {
        yoke->device_address = device_address;
    }
    return ret;
}
