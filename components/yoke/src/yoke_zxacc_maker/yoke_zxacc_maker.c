#include "yoke_zxacc_maker.h"

#include <string.h>

#define ZXACC_REG_FIRMWARE_VERSION 0x00
#define ZXACC_REG_CHARGE_STATUS 0x02
#define ZXACC_REG_VOLTAGE 0x03
#define ZXACC_REG_SHUTDOWN 0x07
#define ZXACC_REG_WAKEUP_TIME 0x08
#define ZXACC_REG_SHUTDOWN_TIME 0x0a
#define ZXACC_REG_TIMER_WAKEUP 0x0c
#define ZXACC_SHUTDOWN_COMMAND 0x01
#define ZXACC_I2C_TIMEOUT_MS 1000

static bool zxacc_is_ready(const yoke_zxacc_maker_t *board)
{
    return board != NULL && board->initialized && board->i2c_device != NULL;
}

static esp_err_t zxacc_read(yoke_zxacc_maker_t *board, uint8_t reg, uint8_t *data, size_t size)
{
    if (!zxacc_is_ready(board) || data == NULL || size == 0) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(board->i2c_device, &reg, sizeof(reg), data, size,
                                       ZXACC_I2C_TIMEOUT_MS);
}

static esp_err_t zxacc_write(yoke_zxacc_maker_t *board, uint8_t reg, const uint8_t *data,
                             size_t size)
{
    if (!zxacc_is_ready(board) || data == NULL || size == 0 || size > 4) return ESP_ERR_INVALID_ARG;

    uint8_t buffer[5] = {reg};
    memcpy(&buffer[1], data, size);
    return i2c_master_transmit(board->i2c_device, buffer, size + 1, ZXACC_I2C_TIMEOUT_MS);
}

static esp_err_t zxacc_read_u16(yoke_zxacc_maker_t *board, uint8_t reg, uint16_t *value)
{
    if (value == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t data[2];
    esp_err_t ret = zxacc_read(board, reg, data, sizeof(data));
    if (ret == ESP_OK) *value = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return ret;
}

static esp_err_t zxacc_write_u16(yoke_zxacc_maker_t *board, uint8_t reg, uint16_t value)
{
    const uint8_t data[] = {(uint8_t)value, (uint8_t)(value >> 8)};
    return zxacc_write(board, reg, data, sizeof(data));
}

yoke_zxacc_maker_config_t yoke_zxacc_maker_default_config(void)
{
    return (yoke_zxacc_maker_config_t){
        .i2c_port = I2C_NUM_0,
        .sda_gpio_num = YOKE_ZXACC_MAKER_DEFAULT_SDA_GPIO,
        .scl_gpio_num = YOKE_ZXACC_MAKER_DEFAULT_SCL_GPIO,
        .i2c_address = YOKE_ZXACC_MAKER_DEFAULT_I2C_ADDR,
        .i2c_clock_hz = 100000,
    };
}

esp_err_t yoke_zxacc_maker_init(yoke_zxacc_maker_t *board,
                                const yoke_zxacc_maker_config_t *config)
{
    if (board == NULL || config == NULL || config->i2c_clock_hz == 0) return ESP_ERR_INVALID_ARG;
    if (board->initialized) return ESP_ERR_INVALID_STATE;

    memset(board, 0, sizeof(*board));
    esp_err_t ret;
    if (config->i2c_bus != NULL) {
        board->i2c_bus = config->i2c_bus;
    } else {
        const i2c_master_bus_config_t bus_config = {
            .i2c_port = config->i2c_port,
            .sda_io_num = config->sda_gpio_num,
            .scl_io_num = config->scl_gpio_num,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ret = i2c_new_master_bus(&bus_config, &board->i2c_bus);
        if (ret != ESP_OK) return ret;
        board->owns_i2c_bus = true;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_address,
        .scl_speed_hz = config->i2c_clock_hz,
    };
    ret = i2c_master_bus_add_device(board->i2c_bus, &device_config, &board->i2c_device);
    if (ret != ESP_OK) {
        if (board->owns_i2c_bus) (void)i2c_del_master_bus(board->i2c_bus);
        memset(board, 0, sizeof(*board));
        return ret;
    }
    board->initialized = true;
    return ESP_OK;
}

esp_err_t yoke_zxacc_maker_deinit(yoke_zxacc_maker_t *board)
{
    if (!zxacc_is_ready(board)) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = i2c_master_bus_rm_device(board->i2c_device);
    if (ret != ESP_OK) return ret;
    if (board->owns_i2c_bus) {
        ret = i2c_del_master_bus(board->i2c_bus);
        if (ret != ESP_OK) return ret;
    }
    memset(board, 0, sizeof(*board));
    return ESP_OK;
}

esp_err_t yoke_zxacc_maker_get_firmware_version(yoke_zxacc_maker_t *board, uint16_t *version)
{
    return zxacc_read_u16(board, ZXACC_REG_FIRMWARE_VERSION, version);
}

esp_err_t yoke_zxacc_maker_get_battery_voltage(yoke_zxacc_maker_t *board, uint32_t *voltage_mv)
{
    if (voltage_mv == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t data[4];
    esp_err_t ret = zxacc_read(board, ZXACC_REG_VOLTAGE, data, sizeof(data));
    if (ret == ESP_OK) {
        *voltage_mv = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                      ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    }
    return ret;
}

esp_err_t yoke_zxacc_maker_get_charge_state(yoke_zxacc_maker_t *board,
                                             yoke_zxacc_maker_charge_state_t *state)
{
    if (state == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t raw;
    esp_err_t ret = zxacc_read(board, ZXACC_REG_CHARGE_STATUS, &raw, sizeof(raw));
    if (ret == ESP_OK) {
        *state = (raw <= YOKE_ZXACC_MAKER_CHARGE_COMPLETE)
                     ? (yoke_zxacc_maker_charge_state_t)raw
                     : YOKE_ZXACC_MAKER_CHARGE_UNKNOWN;
    }
    return ret;
}

esp_err_t yoke_zxacc_maker_get_wakeup_time(yoke_zxacc_maker_t *board, uint16_t *time_ms)
{
    return zxacc_read_u16(board, ZXACC_REG_WAKEUP_TIME, time_ms);
}

esp_err_t yoke_zxacc_maker_set_wakeup_time(yoke_zxacc_maker_t *board, uint16_t time_ms)
{
    return zxacc_write_u16(board, ZXACC_REG_WAKEUP_TIME, time_ms);
}

esp_err_t yoke_zxacc_maker_get_shutdown_time(yoke_zxacc_maker_t *board, uint16_t *time_ms)
{
    return zxacc_read_u16(board, ZXACC_REG_SHUTDOWN_TIME, time_ms);
}

esp_err_t yoke_zxacc_maker_set_shutdown_time(yoke_zxacc_maker_t *board, uint16_t time_ms)
{
    return zxacc_write_u16(board, ZXACC_REG_SHUTDOWN_TIME, time_ms);
}

esp_err_t yoke_zxacc_maker_set_timer_wakeup(yoke_zxacc_maker_t *board, uint32_t seconds)
{
    const uint8_t data[] = {(uint8_t)seconds, (uint8_t)(seconds >> 8),
                            (uint8_t)(seconds >> 16), (uint8_t)(seconds >> 24)};
    return zxacc_write(board, ZXACC_REG_TIMER_WAKEUP, data, sizeof(data));
}

esp_err_t yoke_zxacc_maker_shutdown(yoke_zxacc_maker_t *board)
{
    const uint8_t command = ZXACC_SHUTDOWN_COMMAND;
    return zxacc_write(board, ZXACC_REG_SHUTDOWN, &command, sizeof(command));
}
