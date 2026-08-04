/**
 * @file yoke_ec11.h
 * @brief Yoke-EC11-V10 I2C 外设驱动。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YOKE_EC11_DEVICE_ID              0x4543U
#define YOKE_EC11_DEFAULT_I2C_ADDRESS    0x70U
#define YOKE_EC11_DEFAULT_I2C_SPEED_HZ   100000U
#define YOKE_EC11_DEFAULT_TIMEOUT_MS     1000U

/** 单颗 WS2812 的 RGB 亮度值。 */
typedef struct {
    uint8_t red;                  /**< 红色亮度，范围 0～255。 */
    uint8_t green;                /**< 绿色亮度，范围 0～255。 */
    uint8_t blue;                 /**< 蓝色亮度，范围 0～255。 */
} yoke_ec11_rgb_t;

/** EC11 按键的解析状态。读取后会清除外设中的 press_count。 */
typedef struct {
    bool pressed;                 /**< 当前滤波后的按键状态；true 表示按下。 */
    uint8_t press_count;          /**< 自上次读取后的短按累计次数，读取后外设清零。 */
} yoke_ec11_key_t;

/** 单个外设的配置，供 yoke_ec11_init() 使用。 */
typedef struct {
    uint8_t device_address;       /**< 当前 7 位从机地址。 */
    uint32_t scl_speed_hz;        /**< 此设备的 I2C SCL 通信速率，单位 Hz。 */
    uint32_t timeout_ms;          /**< 单次 I2C 传输超时时间，单位毫秒。 */
    bool verify_device_id;        /**< 初始化时读取并校验 0x4543。 */
} yoke_ec11_config_t;

/** 驱动实例。成员字段应视为私有实现。 */
typedef struct {
    i2c_master_bus_handle_t bus_handle; /**< 应用传入的 I2C 主机总线句柄。 */
    i2c_master_dev_handle_t dev_handle; /**< 本组件创建的 Yoke 从机设备句柄。 */
    uint8_t device_address;             /**< 当前使用的 7 位从机地址。 */
    uint32_t scl_speed_hz;              /**< 当前设备通信速率，单位 Hz。 */
    uint32_t timeout_ms;                /**< 当前单次传输超时时间，单位毫秒。 */
    bool initialized;                   /**< 驱动是否已成功初始化。 */
} yoke_ec11_t;

/** 返回默认设备配置：地址 0x70、速率 100 kHz。 */
yoke_ec11_config_t yoke_ec11_default_config(void);

/**
 * 将一颗 Yoke-EC11-V10 挂载到已有的 ESP-IDF I2C 主机总线。
 * 应用程序保留 bus_handle 的所有权，并可继续在此总线上挂载其他 I2C 设备。
 */
esp_err_t yoke_ec11_init(yoke_ec11_t *yoke,
                          i2c_master_bus_handle_t bus_handle,
                          const yoke_ec11_config_t *config);

/** 仅从 I2C 总线上移除本设备；总线仍可供其他设备使用。 */
esp_err_t yoke_ec11_deinit(yoke_ec11_t *yoke);

/** 检查当前配置的从机是否响应 I2C。 */
esp_err_t yoke_ec11_probe(const yoke_ec11_t *yoke);

/** 读取并返回固定设备 ID（期望值：0x4543）。 */
esp_err_t yoke_ec11_read_device_id(const yoke_ec11_t *yoke, uint16_t *device_id);

/** 读取按键状态。读取后外设会清除累计的 press_count。 */
esp_err_t yoke_ec11_read_key(const yoke_ec11_t *yoke, yoke_ec11_key_t *key);

/** 返回当前/最近一次按键时长，单位为毫秒，分辨率为 100 毫秒。 */
esp_err_t yoke_ec11_read_key_press_time_ms(const yoke_ec11_t *yoke, uint16_t *time_ms);

/** 读取有符号、持续累计的编码器计数值。 */
esp_err_t yoke_ec11_read_encoder_count(const yoke_ec11_t *yoke, int16_t *count);

/** 读取有符号编码器增量。该操作会清除外设的增量计数器。 */
esp_err_t yoke_ec11_read_encoder_diff(const yoke_ec11_t *yoke, int16_t *diff);

/** 设置单颗 WS2812：索引 0 为 LED1（远离连接器），索引 1 为 LED2（靠近连接器）。 */
esp_err_t yoke_ec11_set_ws2812(const yoke_ec11_t *yoke, uint8_t index,
                                yoke_ec11_rgb_t color);

/** 通过 I2C 写入设置两颗 WS2812。 */
esp_err_t yoke_ec11_set_ws2812_all(const yoke_ec11_t *yoke,
                                    yoke_ec11_rgb_t led1,
                                    yoke_ec11_rgb_t led2);

/** 读取单颗 WS2812 颜色；索引定义与 yoke_ec11_set_ws2812() 相同。 */
esp_err_t yoke_ec11_get_ws2812(const yoke_ec11_t *yoke, uint8_t index,
                                yoke_ec11_rgb_t *color);

/** 读写 CTRL1；写入 0x5A 会让外设进入测试模式。 */
esp_err_t yoke_ec11_read_ctrl1(const yoke_ec11_t *yoke, uint8_t *value);
esp_err_t yoke_ec11_write_ctrl1(const yoke_ec11_t *yoke, uint8_t value);
esp_err_t yoke_ec11_enter_test_mode(const yoke_ec11_t *yoke);

/**
 * 将新的从机地址低 3 位保存至外设 FLASH。
 * 根据设备规格，复位外设后该配置才会生效。
 */
esp_err_t yoke_ec11_configure_slave_address(const yoke_ec11_t *yoke,
                                              uint8_t address_low_3bits);

/** 仅更新主机目标地址，例如在复位外设后调用。 */
esp_err_t yoke_ec11_set_active_address(yoke_ec11_t *yoke, uint8_t device_address);

#ifdef __cplusplus
}
#endif

