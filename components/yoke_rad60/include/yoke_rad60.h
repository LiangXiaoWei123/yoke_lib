#ifndef __YOKE_RAD60_RADAR_H__
#define __YOKE_RAD60_RADAR_H__

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdbool.h>

#define YOKE_RAD60_QUERY_INTERVAL_MS 50

/* ========================================= 事件组定义 ========================================= */
// 事件组位定义
#define YOKE_RAD60_RX_DATA_RECEIVED_BIT (1 << 0)  // 接收到雷达数据事件

/* ========================================= 配置常量 ========================================= */

// 命令响应超时配置
#define YOKE_RAD60_CMD_TIMEOUT_MS      1000
#define YOKE_RAD60_CMD_RETRY_COUNT     3

// UART配置默认值
#define YOKE_RAD60_UART_NUM_DEFAULT    UART_NUM_1
#define YOKE_RAD60_TX_PIN_DEFAULT      15
#define YOKE_RAD60_RX_PIN_DEFAULT      16
#define YOKE_RAD60_BAUDRATE_DEFAULT    921600

// 任务配置
#define YOKE_RAD60_TASK_STACK_SIZE     (3 * 1024)
#define YOKE_RAD60_TASK_PRIORITY       9

/* ========================================= 命令枚举定义 ========================================= */

// 基本命令枚举 (0x00-0x1F)
typedef enum {
    // 调试命令
    YOKE_RAD60_CMD_REG_WRITE        = 0x00,  // 寄存器写命令
    YOKE_RAD60_CMD_REG_READ         = 0x01,  // 寄存器读命令
    YOKE_RAD60_CMD_SET_SENSE_LEVEL  = 0x02,  // 设置雷达感应等级
    YOKE_RAD60_CMD_GET_SENSE_LEVEL  = 0x03,  // 获取雷达感应等级
    
    // 基本配置命令
    YOKE_RAD60_CMD_SET_SENSE_TIME   = 0x04,  // 设置感应电平持续时间
    YOKE_RAD60_CMD_GET_SENSE_TIME   = 0x05,  // 获取感应电平持续时间
    YOKE_RAD60_CMD_SET_LIGHT_THRES  = 0x06,  // 设置光敏阈值
    YOKE_RAD60_CMD_GET_LIGHT_THRES  = 0x07,  // 获取光敏阈值
    YOKE_RAD60_CMD_SAVE_SETTINGS    = 0x08,  // 保存雷达设置
    YOKE_RAD60_CMD_GET_SAVE_STATUS  = 0x09,  // 获取雷达保存状态
    YOKE_RAD60_CMD_SET_OUT_STATE    = 0x0A,  // 设置OUT信号状态
    YOKE_RAD60_CMD_SET_PWM_DUTY     = 0x0B,  // 设置PWM占空比
    
    // 调试命令（续）
    YOKE_RAD60_CMD_MEM_WRITE        = 0x10,  // 内存写命令
    YOKE_RAD60_CMD_MEM_READ         = 0x11,  // 内存读命令
    
    // 系统命令
    YOKE_RAD60_CMD_SYSTEM_RESET     = 0x13,  // 系统复位
    YOKE_RAD60_CMD_FLASH_WRITE      = 0x14,  // Flash写命令
    
    // 波特率命令
    YOKE_RAD60_CMD_SET_BAUD_RATE    = 0x19,  // 波特率切换
    
    // 版本命令
    // YOKE_RAD60_CMD_GET_VERSION   = 0x1E   // 获取软硬件版本号
} yoke_rad60_basic_cmd_t;

// 雷达配置命令枚举 (0x30-0x3F)
typedef enum {
    YOKE_RAD60_CMD_GET_RADAR_INFO   = 0x30,  // 获取雷达感应信息
    YOKE_RAD60_CMD_GET_ALGO_TYPE    = 0x31,  // 获取当前算法类型
    YOKE_RAD60_CMD_GET_BOUNDARY     = 0x32,  // 获取算法配置的边界值
    YOKE_RAD60_CMD_GET_SENSE_CONFIG = 0x33,  // 获取算法感应配置
    YOKE_RAD60_CMD_SET_MOT_NEAR     = 0x34,  // 设置运动检测最近感应距离
    YOKE_RAD60_CMD_SET_MOT_SENS     = 0x35,  // 设置运动检测灵敏度
    YOKE_RAD60_CMD_SET_MICRO_FAR    = 0x36,  // 设置微动检测最远感应距离
    YOKE_RAD60_CMD_SET_MICRO_NEAR   = 0x37,  // 设置微动检测最近感应距离
    YOKE_RAD60_CMD_SET_MICRO_SENS   = 0x38,  // 设置微动检测灵敏度
    YOKE_RAD60_CMD_SET_BREATH_FAR   = 0x39,  // 设置呼吸检测最远感应距离
    YOKE_RAD60_CMD_SET_BREATH_NEAR  = 0x3A,  // 设置呼吸检测最近感应距离
    YOKE_RAD60_CMD_SET_BREATH_SENS  = 0x3B   // 设置呼吸检测灵敏度
} yoke_rad60_radar_cfg_cmd_t;

// 雷达控制命令枚举 (0xD0-0xDF)
typedef enum {
    YOKE_RAD60_CMD_GET_RADAR_STATE  = 0xD0,  // 获取雷达感应状态
    YOKE_RAD60_CMD_SET_RADAR_ENABLE = 0xD1,  // 设置雷达感应功能
    YOKE_RAD60_CMD_SET_MOT_FAR      = 0xD2   // 设置运动检测最远感应距离
} yoke_rad60_radar_ctrl_cmd_t;

// 超低功耗命令枚举 (0x90-0x9F)
typedef enum {
    YOKE_RAD60_CMD_SET_ULP_TIME     = 0x90,  // 设置超低功耗时间
    YOKE_RAD60_CMD_READ_AT5815C     = 0x91,  // 读取AT5815C
    YOKE_RAD60_CMD_WRITE_AT5815C    = 0x92,  // 写入AT5815C
    YOKE_RAD60_CMD_ENTER_COMMUNICATION_MODE = 0xE0,  // 进入/退出通信模式
} yoke_rad60_ulp_cmd_t;

//手扫参数配置命令
typedef enum {
    YOKE_RAD60_CMD_SET_HAND_DIST_LEVEL = 0xD4,      // 3.4.1 设置挥手距离档位
    YOKE_RAD60_CMD_GET_HAND_DIST_LEVEL = 0xD5,      // 3.4.2 获取挥手距离档位
    YOKE_RAD60_CMD_SET_HAND_VEL_LEVEL = 0xD6,       // 3.4.3 设置挥手速度
    YOKE_RAD60_CMD_GET_HAND_VEL_LEVEL = 0xD7,       // 3.4.4 获取挥手速度
    YOKE_RAD60_CMD_SET_HAND_ANGLE = 0xD8,           // 3.4.5 设置挥手角度
    YOKE_RAD60_CMD_GET_HAND_ANGLE = 0xD9,           // 3.4.6 获取挥手角度
    YOKE_RAD60_CMD_SET_HUMAN_DIST = 0xDA,           // 3.4.7 设置人感距离
    YOKE_RAD60_CMD_GET_HUMAN_DIST = 0xDB,           // 3.4.8 获取人感距离
    YOKE_RAD60_CMD_SET_HUMAN_SENSITIVITY = 0xDC,    // 3.4.9 设置人感灵敏度
    YOKE_RAD60_CMD_GET_HUMAN_SENSITIVITY = 0xDD,    // 3.4.10 获取人感灵敏度
    YOKE_RAD60_CMD_BAUD_RATE_SWITCH = 0x19,         // 3.4.11 波特率切换
    YOKE_RAD60_CMD_SAVE_RADAR_CONFIG = 0x08,        // 3.4.12 保存雷达配置
    YOKE_RAD60_CMD_RESTORE_DEFAULT_SETTINGS = 0x15  // 3.4.13 恢复默认设置
} yoke_rad60_hand_scan_param_cmd_t;

/* ========================================= 雷达感应信息结构体定义 ========================================= */

typedef struct {
    uint8_t is_detected; /* 运动,微动和存在综合判定结果 */

    uint8_t det_result; /* 算法检测状态： 0x01: 靠近, 0x02: 远离, 0x04: 运动, 0x08: 微动, 0x10: 呼吸 */
    uint16_t range_val; /* 检测距离值,单位: mm */
    int16_t angle_val; /* 检测角度值,单位: 1degree */
    int16_t velo_val; /* 检测速度值,目前预留. */
    uint8_t reserved[6]; /* 预留待扩展. */
    uint8_t rb_conf; /* 综合距离置信度,范围 0~16,若<12,距离值可能不准确 */
    uint8_t angle_conf; /* 综合角度置信度,范围 0~16,若<8, 距离值可能不准确 */
    uint32_t frame_idx; /* 帧索引 */
} fmcw_det_info_t;

/* ========================================= 上报帧信息结构体 ========================================= */

//type=0x01
typedef struct {
    uint16_t htm; /* 身高 */
    uint8_t status; /* 0:invalid,1: calib done,2:enter htm, 3:htm done*/
    uint8_t reserved;
} htm_det_info_t;

//type=0x02
typedef struct {
    int8_t rb;         /* Rb 编号 */
    uint8_t pwr_db_raw; /* 原始能量值,单位 db */
    uint8_t pwr_db_rgc; /* 带距离补偿后的能量值,单位 db */
    uint8_t num_rb_greater_than_thr; /* 本 RB 之后的 refRBCount 之中,有多少个能量大于能量门限 threshold_DB */
} pod_det_info_t;

//type=0x03
typedef struct {
    uint8_t is_detected; /* 运动,微动和存在综合判定结果 */
    uint8_t det_result; /* 算法检测状态： 0x01: 靠近 0x02: 远离 0x04: 运动 0x08：微动 0x10：呼吸 */
    uint16_t range_val; /* 检测距离值,单位: mm */
    int16_t angle_val; /* 检测角度值,单位: 1degree */
    int16_t velo_val; /* 检测速度值,目前预留 */
} mot_det_info_t;

//type=0x04
typedef struct {
    uint8_t det_result; /* detection result */
    uint8_t br_val; /* breathing rate */
    uint8_t hr_val; /* heart rate */
    uint8_t angle_val; /* reserve */
    uint16_t range_val; /* range val mm */
    uint16_t padding; /* reserve */
} bhr_det_info_t;

//type=0x05
typedef struct {
    uint16_t range_val;
    int16_t angle_val;
} rgn_obj_info_t;
typedef struct {
    uint32_t obj_num; /* Number of object,only output the nearest objectin each region,max obj num = 3 */
    rgn_obj_info_t obj[3]; /* Detection information of the nearest object in each region, max region = 3 */
} rgn_det_info_t;

typedef struct {
 uint16_t range_val; /* 距离值,单位: cm */
 int16_t angle_val; /* 角度值,单位: 1° */
 int16_t velo_val; /* 速度值,单位: cm/s */
 uint8_t objid; /* 目标ID */
 uint8_t reserved0; /* 暂无含义 */
} multitarget_obj_info_t;
typedef struct {
  uint8_t obj_num; /* 目标数量 */
  uint8_t reserved[3]; /* 暂无含义 */
  multitarget_obj_info_t obj[2]; /* 目标信息 暂时最多支持2个目标*/
} multitarget_det_info_t;

/* ========================================= yoke_rad60雷达数据定义 ========================================= */

// 串口通信状态
typedef struct {
    bool is_comm_ok;                   // 通信是否正常
    bool is_detected;                  // 是否检测到目标
    multitarget_det_info_t radar_info;        // 雷达数据
} yoke_rad60_radar_data_t;

/**
 * @brief 获取雷达数据（非阻塞）
 * @param data 输出参数，雷达数据
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_get_radar_data(yoke_rad60_radar_data_t* data);

/* ========================================= 函数声明 对外接口 ========================================= */

/**
 * @brief 初始化雷达模块
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_init(uint8_t uart_num, int16_t tx_pin, int16_t rx_pin, uint32_t boudrate);

/**
 * @brief 注销雷达模块，释放资源
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_deinit(void);

/* ========== 基本配置命令 ========== */

/**
 * @brief 设置感应电平持续时间
 * @param sense_time 持续时间(ms)
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_sense_time(uint16_t sense_time);

/**
 * @brief 获取感应电平持续时间
 * @param sense_time 输出参数，持续时间(ms)
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_get_sense_time(uint16_t* sense_time);

/**
 * @brief 设置光敏阈值
 * @param light_thres 光敏阈值
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_light_thres(uint16_t light_thres);

/**
 * @brief 获取光敏阈值
 * @param light_thres 输出参数，光敏阈值
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_get_light_thres(uint16_t* light_thres);

/**
 * @brief 保存雷达设置到flash
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_save_settings(void);

/**
 * @brief 获取雷达保存状态
 * @param status 输出参数，保存状态
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_get_save_status(uint8_t* status);

/**
 * @brief 设置OUT信号状态
 * @param out_state OUT信号状态
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_out_state(uint8_t out_state);

/**
 * @brief 设置PWM占空比
 * @param pwm_duty PWM占空比 (0-100)
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_pwm_duty(uint8_t pwm_duty);

/**
 * @brief 系统复位
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_system_reset(void);

/**
 * @brief 波特率切换
 * @param baud_rate 波特率
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_baud_rate(uint32_t baud_rate);

/* ========== 雷达配置命令 ========== */

/**
 * @brief 设置雷达感应等级
 * @param level 感应等级
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_sense_level(uint8_t level);

/**
 * @brief 获取雷达感应等级
 * @param level 输出参数，感应等级
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_get_sense_level(uint8_t* level);

/**
 * @brief 获取雷达感应信息（从缓存读取，非阻塞）
 * @param info 输出参数，雷达感应信息
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_get_radar_info(fmcw_det_info_t* info);

/**
 * @brief 获取当前算法类型
 * @param algo_type 输出参数，算法类型
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_get_algo_type(uint8_t* algo_type);

/**
 * @brief 获取算法配置的边界值
 * @param boundary 输出参数，边界值
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_get_boundary(uint8_t* boundary);

/**
 * @brief 设置运动检测最近感应距离
 * @param mot_near 最近距离(mm)
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_mot_near(uint16_t mot_near);

/**
 * @brief 设置运动检测灵敏度
 * @param mot_sens 灵敏度
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_mot_sens(uint8_t mot_sens);

/**
 * @brief 设置呼吸检测最远感应距离
 * @param breath_far 最远距离(mm)
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_breath_far(uint16_t breath_far);

/**
 * @brief 设置呼吸检测最近感应距离
 * @param breath_near 最近距离(mm)
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_breath_near(uint16_t breath_near);

/**
 * @brief 设置呼吸检测灵敏度
 * @param breath_sens 灵敏度
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_breath_sens(uint8_t breath_sens);

/* ========== 雷达控制命令 ========== */

/**
 * @brief 获取雷达感应状态
 * @param state 输出参数，雷达状态
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_get_radar_state(uint8_t* state);

/**
 * @brief 设置雷达感应功能
 * @param enable 1=开启, 0=关闭
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_radar_enable(uint8_t enable);

/**
 * @brief 设置运动检测最远感应距离
 * @param mot_far 最远距离(mm)
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_mot_far(uint16_t mot_far);

/* ========== 超低功耗命令 ========== */

/**
 * @brief 设置超低功耗时间
 * @param ulp_time 超低功耗时间
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_set_ulp_time(uint16_t ulp_time);

/**
 * @brief 读取AT5815C
 * @param data 输出缓冲区
 * @param len 输出数据长度
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_read_at5815c(uint8_t* data, uint32_t* len);

/**
 * @brief 写入AT5815C
 * @param data 写入数据
 * @param len 写入数据长度
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_write_at5815c(const uint8_t* data, uint32_t len);

/**
 * @brief 重新配置YOKE_RAD60雷达的UART引脚（不删除任务，轻量级切换）
 * @param tx_pin 新的TX引脚
 * @param rx_pin 新的RX引脚
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_reconfigure_uart(int16_t tx_pin, int16_t rx_pin);

//辅助函数:状态转字符串
char* det_status_to_string(uint8_t det_result);

//设置雷达参数
void set_radar_config(void);

#endif
