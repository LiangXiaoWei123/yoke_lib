/** @file yoke_rad60.c */
#include "yoke_rad60.h"
#include "yoke_rad60_protocol.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "yoke_rad60";

/* ========================================= 全局变量 ========================================= */

// 互斥锁保护共享资源
static SemaphoreHandle_t radar_mutex = NULL;

// 命令发送互斥锁（保护命令发送和响应等待的完整过程）
static SemaphoreHandle_t cmd_send_mutex = NULL;

// 事件组（用于通知UI任务接收到数据）
static EventGroupHandle_t radar_event_group = NULL;

// 雷达任务句柄
static TaskHandle_t yoke_rad60_task_handle = NULL;

static BaseType_t yoke_rad60_create_task(TaskFunction_t function, const char *name,
                                         uint32_t stack_size, UBaseType_t priority,
                                         TaskHandle_t *handle)
{
#if CONFIG_YOKE_BSP_RAD60_TASK_STACKS_IN_PSRAM
    return xTaskCreatePinnedToCoreWithCaps(function, name, stack_size, NULL, priority,
                                           handle, tskNO_AFFINITY,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return xTaskCreate(function, name, stack_size, NULL, priority, handle);
#endif
}

static void yoke_rad60_delete_task(TaskHandle_t task_handle)
{
#if CONFIG_YOKE_BSP_RAD60_TASK_STACKS_IN_PSRAM
    vTaskDeleteWithCaps(task_handle);
#else
    vTaskDelete(task_handle);
#endif
}

// 雷达初始化时间戳(用于给雷达启动留出宽容期)
static uint32_t radar_init_time_ms = 0;

// 雷达信息缓存（用于快速读取）
static fmcw_det_info_t radar_info;

// 多目标检测信息缓存（用于快速读取）
static yoke_rad60_radar_data_t yoke_rad60_radar_data = {
    .is_comm_ok = true,
};

// 命令请求结构体（用于同步等待响应）
typedef struct {
    uint8_t cmd;                    // 等待的命令
    bool waiting;                   // 是否在等待响应
    SemaphoreHandle_t response_sem; // 响应信号量
    uint8_t* response_data;         // 响应数据指针
    uint32_t response_len;          // 响应数据长度
    esp_err_t result;               // 执行结果
} cmd_request_t;

static cmd_request_t cmd_request = { 0 };

/* ========================================= 前向声明 ========================================= */
static void yoke_rad60_task(void* arg);
esp_err_t yoke_rad60_process_data(uart_frame_t* frame);

static inline void yoke_rad60_pack_le16(uint8_t* dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
}

static inline void yoke_rad60_pack_le32(uint8_t* dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

static inline uint16_t yoke_rad60_unpack_le16(const uint8_t* src) {
    return (uint16_t)(src[0] | (src[1] << 8));
}

/* ========================================= 核心辅助函数 ========================================= */

/**
 * @brief 发送命令并等待响应（同步模式）
 *
 * @param cmd 完整命令码
 * @param tx_data 发送数据
 * @param tx_len 发送数据长度
 * @param rx_buffer 接收缓冲区（调用者分配）
 * @param rx_len 接收数据长度（输出参数，可为NULL）
 * @param timeout_ms 超时时间
 * @return esp_err_t
 */
static esp_err_t yoke_rad60_send_cmd_sync(
    uint8_t cmd,
    const uint8_t* tx_data,
    uint32_t tx_len,
    uint8_t* rx_buffer,
    uint32_t* rx_len,
    uint32_t timeout_ms) {
    if (cmd_request.response_sem == NULL) {
        ESP_LOGE(TAG, "Response semaphore not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 获取命令发送互斥锁,确保同一时间只有一个任务在发送命令并等待响应
    // 超时时间设为5秒,足够长以避免正常情况下的超时
    if (xSemaphoreTake(cmd_send_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire command send mutex");
        return ESP_ERR_TIMEOUT;
    }

    // 设置等待状态
    if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex");
        xSemaphoreGive(cmd_send_mutex);
        return ESP_ERR_TIMEOUT;
    }

    cmd_request.cmd = cmd;
    cmd_request.waiting = true;
    cmd_request.response_data = rx_buffer;
    cmd_request.response_len = 0;
    cmd_request.result = ESP_ERR_TIMEOUT;

    xSemaphoreGive(radar_mutex);

    // 发送命令
    yoke_rad60_protocol_write_bytes(cmd, tx_data, tx_len);

    // 等待响应（由统一的响应处理任务处理）
    if (xSemaphoreTake(cmd_request.response_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        // 获取结果
        if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to acquire mutex after response");
            xSemaphoreGive(cmd_send_mutex);
            return ESP_ERR_TIMEOUT;
        }

        esp_err_t result = cmd_request.result;
        if (rx_len) {
            *rx_len = cmd_request.response_len;
        }
        cmd_request.waiting = false;

        xSemaphoreGive(radar_mutex);
        xSemaphoreGive(cmd_send_mutex);

        return result;
    }

    // 超时
    if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        cmd_request.waiting = false;
        xSemaphoreGive(radar_mutex);
    }

    xSemaphoreGive(cmd_send_mutex);

    ESP_LOGW(TAG, "Command 0x%02x timeout", cmd);
    return ESP_ERR_TIMEOUT;
}

/* ========================================= 响应处理函数 ========================================= */

//响应帧处理函数：根据响应帧的命令码处理 -- 帧头为0x59
static esp_err_t yoke_rad60_process_response_frame(uint8_t cmd, uint8_t len, uint8_t* buffer) {
    // 先检查是否有命令在等待响应（通用逻辑）
    if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (cmd_request.waiting && cmd_request.cmd == cmd) {
            // 复制响应数据
            if (cmd_request.response_data && buffer && len > 0) {
                uint32_t copy_len = len > 64 ? 64 : len;
                memcpy(cmd_request.response_data, buffer, copy_len);
                cmd_request.response_len = copy_len;
            }
            cmd_request.result = ESP_OK;
            // 释放等待的任务
            xSemaphoreGive(cmd_request.response_sem);
        }
        xSemaphoreGive(radar_mutex);
    } else {
        ESP_LOGW(TAG, "Failed to acquire mutex in response handler, cmd=0x%02x may timeout", cmd);
    }

    // 根据具体命令处理（主要是日志和特殊处理）
    switch (cmd) {
        // ===== 基本命令 (0x00-0x1F) =====
        case YOKE_RAD60_CMD_SET_SENSE_LEVEL:
            ESP_LOGD(TAG, "设置雷达感应等级: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_GET_SENSE_LEVEL:
            if (len >= 1) {
                ESP_LOGD(TAG, "获取雷达感应等级: %d", buffer[0]);
            }
            break;
        case YOKE_RAD60_CMD_SET_SENSE_TIME:
            ESP_LOGD(TAG, "设置感应电平持续时间: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_GET_SENSE_TIME:
            if (len >= 2) {
                ESP_LOGD(TAG, "获取感应电平持续时间: %d", yoke_rad60_unpack_le16(buffer));
            }
            break;
        case YOKE_RAD60_CMD_SET_LIGHT_THRES:
            ESP_LOGD(TAG, "设置光敏阈值: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_GET_LIGHT_THRES:
            if (len >= 2) {
                ESP_LOGD(TAG, "获取光敏阈值: %d", yoke_rad60_unpack_le16(buffer));
            }
            break;
        case YOKE_RAD60_CMD_SAVE_SETTINGS:
            ESP_LOGD(TAG, "保存雷达设置: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_GET_SAVE_STATUS:
            if (len >= 1) {
                ESP_LOGD(TAG, "获取雷达保存状态: 0x%02x", buffer[0]);
            }
            break;
        case YOKE_RAD60_CMD_SET_OUT_STATE:
            ESP_LOGD(TAG, "设置OUT信号状态: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_SET_PWM_DUTY:
            ESP_LOGD(TAG, "设置PWM占空比: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_SYSTEM_RESET:
            ESP_LOGD(TAG, "系统复位");
            break;
        case YOKE_RAD60_CMD_SET_BAUD_RATE:
            ESP_LOGD(TAG, "波特率切换: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;

        // ===== 雷达配置命令 (0x30-0x3F) =====
        case YOKE_RAD60_CMD_GET_RADAR_INFO:
            if (len >= 20) {
                // 更新缓存（加锁保护）
                if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    if (buffer[14] < 12) {
                        ESP_LOGW(TAG, "距离置信度(%d)<12, 距离值可能不准确", buffer[14]);
                        xSemaphoreGive(radar_mutex);
                        break;
                    }
                    if (buffer[15] < 8) {
                        ESP_LOGW(TAG, "角度置信度(%d)<8, 角度值可能不准确", buffer[15]);
                        xSemaphoreGive(radar_mutex);
                        break;
                    }
                    radar_info.is_detected = buffer[0];
                    radar_info.det_result = buffer[1];
                    radar_info.range_val = (uint16_t)(buffer[2] | (buffer[3] << 8));
                    radar_info.angle_val = (int16_t)(buffer[4] | (buffer[5] << 8));
                    radar_info.velo_val = (int16_t)(buffer[6] | (buffer[7] << 8));
                    memcpy(radar_info.reserved, &buffer[8], 6);
                    radar_info.rb_conf = buffer[14];
                    radar_info.angle_conf = buffer[15];
                    radar_info.frame_idx = (uint32_t)(buffer[16] | (buffer[17] << 8) |
                        (buffer[18] << 16) | (buffer[19] << 24));

                    xSemaphoreGive(radar_mutex);

                    // 条件编译日志
#if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
                    const char* det_status = "未知";
                    switch (radar_info.det_result) {
                        case 0x01: det_status = "靠近"; break;
                        case 0x02: det_status = "远离"; break;
                        case 0x04: det_status = "运动"; break;
                        case 0x08: det_status = "微动"; break;
                        case 0x10: det_status = "呼吸"; break;
                    }
                    ESP_LOGD(TAG, "检测状态: %s, 距离: %dmm, 角度: %d°, 帧索引: %"PRIu32"",
                        det_status, radar_info.range_val, radar_info.angle_val, radar_info.frame_idx);
#endif
                }
            }
            break;
        case YOKE_RAD60_CMD_GET_ALGO_TYPE:
            if (len >= 1) {
                ESP_LOGD(TAG, "获取当前算法类型: 0x%02x", buffer[0]);
            }
            break;
        case YOKE_RAD60_CMD_GET_BOUNDARY:
            ESP_LOGD(TAG, "获取算法配置的边界值");
            break;
        case YOKE_RAD60_CMD_GET_SENSE_CONFIG:
            ESP_LOGD(TAG, "获取算法感应配置");
            break;
        case YOKE_RAD60_CMD_SET_MOT_NEAR:
            ESP_LOGD(TAG, "设置运动检测最近感应距离: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_SET_MOT_SENS:
            ESP_LOGD(TAG, "设置运动检测灵敏度: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_SET_MICRO_FAR:
            ESP_LOGD(TAG, "设置微动检测最远感应距离: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_SET_MICRO_NEAR:
            ESP_LOGD(TAG, "设置微动检测最近感应距离: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_SET_MICRO_SENS:
            ESP_LOGD(TAG, "设置微动检测灵敏度: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_SET_BREATH_FAR:
            ESP_LOGD(TAG, "设置呼吸检测最远感应距离: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_SET_BREATH_NEAR:
            ESP_LOGD(TAG, "设置呼吸检测最近感应距离: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_SET_BREATH_SENS:
            ESP_LOGD(TAG, "设置呼吸检测灵敏度: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;

        // ===== 超低功耗命令 (0x90-0x9F) =====
        case YOKE_RAD60_CMD_SET_ULP_TIME:
            ESP_LOGD(TAG, "设置超低功耗时间: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_READ_AT5815C:
            ESP_LOGD(TAG, "读取AT5815C: len=%d", len);
            break;
        case YOKE_RAD60_CMD_WRITE_AT5815C:
            ESP_LOGD(TAG, "写入AT5815C: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;

        // ===== 雷达控制命令 (0xD0-0xDF) =====
        case YOKE_RAD60_CMD_GET_RADAR_STATE:
            if (len >= 1) {
                ESP_LOGD(TAG, "获取雷达感应状态: 0x%02x", buffer[0]);
                // 收到查询响应，说明通信正常
                if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    yoke_rad60_radar_data.is_comm_ok = true;
                    xSemaphoreGive(radar_mutex);
                }
            }
            break;
        case YOKE_RAD60_CMD_SET_RADAR_ENABLE:
            ESP_LOGD(TAG, "设置雷达感应功能: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;
        case YOKE_RAD60_CMD_SET_MOT_FAR:
            ESP_LOGD(TAG, "设置运动检测最远感应距离: %s", (len > 0 && buffer[0] == 0) ? "成功" : "失败");
            break;

        default:
            ESP_LOGE(TAG, "Unknown command response: 0x%02x", cmd);
            return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

//处理多目标检测信息
static void yoke_rad60_process_multitarget_det_info(uint8_t len, uint8_t* buffer) {
    ESP_LOGI(TAG, "处理多目标检测信息len=%d", len);

    if(xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // // 解析目标数量
        yoke_rad60_radar_data.radar_info.obj_num = buffer[0];
        ESP_LOGI(TAG, "目标数量=%d", yoke_rad60_radar_data.radar_info.obj_num);
        
        // buffer[1] - buffer[3] 暂无含义
        
        // 限制解析的目标数量，防止越界
        if (yoke_rad60_radar_data.radar_info.obj_num > 2) {
            ESP_LOGW(TAG, "目标数量%d超过最大支持数2, 仅解析前2个目标", yoke_rad60_radar_data.radar_info.obj_num);
        }

        for (uint8_t i = 0; i < yoke_rad60_radar_data.radar_info.obj_num; i++) {
            // 计算当前目标数据块的起始索引，每个目标占4字节
            uint8_t data_index = 4 + (i * 4); // 4字节头部后，每个目标数据块4字节

            // 解析距离
            if (buffer[data_index] < 32) {
                yoke_rad60_radar_data.radar_info.obj[i].range_val = buffer[data_index] * 6.25;
            } else {
                yoke_rad60_radar_data.radar_info.obj[i].range_val = buffer[data_index] * 6.85;
            }
            
            // 解析角度 (data_index + 1)
            yoke_rad60_radar_data.radar_info.obj[i].angle_val = (int8_t)buffer[data_index + 1]; // 注意可能为有符号数
            
            // 解析速度 (data_index + 2)
            yoke_rad60_radar_data.radar_info.obj[i].velo_val = (int8_t)(buffer[data_index + 2] / 32);
            
            // 解析ID (data_index + 3)
            yoke_rad60_radar_data.radar_info.obj[i].objid = buffer[data_index + 3];

            ESP_LOGI(TAG, "目标%d: 距离=%d cm, 角度=%d度, 速度=%d m/s", 
                i + 1,
                yoke_rad60_radar_data.radar_info.obj[i].range_val,
                yoke_rad60_radar_data.radar_info.obj[i].angle_val,
                yoke_rad60_radar_data.radar_info.obj[i].velo_val);
        }

        // //buffer[4]是距离range，如果值range<32,最终距离为range*6.25，反之，最终距离为range*6.85
        // if (buffer[4] < 32) {
        //     yoke_rad60_radar_data.radar_info.obj[0].range_val = buffer[4] * 6.25;
        // } else {
        //     yoke_rad60_radar_data.radar_info.obj[0].range_val = buffer[4] * 6.85;
        // }
        // //buffer[5]就是角度
        // yoke_rad60_radar_data.radar_info.obj[0].angle_val = buffer[5];
        // //buffer[6]是速度，最终速度 = buffer[6] / 32;单位:m/s
        // yoke_rad60_radar_data.radar_info.obj[0].velo_val = (int16_t)(buffer[6] / 32);
        // //buffer[7]是ID，目前无效，所以为0
        // yoke_rad60_radar_data.radar_info.obj[0].objid = buffer[7];
        
        // ESP_LOGI(TAG, "目标1: 距离=%d cm, 角度=%d度, 速度=%d m/s", 
        //         yoke_rad60_radar_data.radar_info.obj[0].range_val,
        //         yoke_rad60_radar_data.radar_info.obj[0].angle_val,
        //         yoke_rad60_radar_data.radar_info.obj[0].velo_val);
        
        // // 循环解析每个目标，从buffer[4]开始，每个目标占8字节
        // // 布局: 距离(2字节) + 角度(2字节) + 速度(2字节) + ID(1字节) + reserved0(1字节)
        // // 协议采用大端序，ESP32是小端架构，需要进行字节序转换
        // for (uint8_t i = 0; i < yoke_rad60_radar_data.radar_info.obj_num; i++) {
        //     uint32_t offset = 4 + i * 8;  // 前4字节为目标数量和保留字段
            
        //     // 从大端序转换为小端序
        //     yoke_rad60_radar_data.radar_info.obj[i].range_val = (buffer[offset] << 8) | buffer[offset + 1];
        //     yoke_rad60_radar_data.radar_info.obj[i].angle_val = (int16_t)((buffer[offset + 2] << 8) | buffer[offset + 3]);
        //     yoke_rad60_radar_data.radar_info.obj[i].velo_val = (int16_t)((buffer[offset + 4] << 8) | buffer[offset + 5]);
        //     yoke_rad60_radar_data.radar_info.obj[i].objid = buffer[offset + 6];
        //     yoke_rad60_radar_data.radar_info.obj[i].reserved0 = buffer[offset + 7];

        //     //小端序直接转换
        //     // memcpy(&yoke_rad60_radar_data.radar_info.obj[i], &buffer[offset], sizeof(multitarget_obj_info_t));
            
        //     // 打印日志
        //     ESP_LOGI(TAG, "目标%d: 距离=%d cm, 角度=%d度, 速度=%d cm/s, ID=%d", 
        //             i + 1,
        //             yoke_rad60_radar_data.radar_info.obj[i].range_val,
        //             yoke_rad60_radar_data.radar_info.obj[i].angle_val,
        //             yoke_rad60_radar_data.radar_info.obj[i].velo_val,
        //             yoke_rad60_radar_data.radar_info.obj[i].objid);
        // }
        
        // 根据目标数量设置 is_detected
        if (yoke_rad60_radar_data.radar_info.obj_num > 0) {
            yoke_rad60_radar_data.is_detected = 1;
        } else {
            yoke_rad60_radar_data.is_detected = 0;
        }
        
        xSemaphoreGive(radar_mutex);
    }
}

//上报帧处理函数：根据上报帧的类型处理 --   帧头为0x5A  处理主动上报帧
static esp_err_t yoke_rad60_process_report_frame(uint8_t len, uint8_t type, uint8_t* buffer) {
    ESP_LOGI(TAG, "处理主动上报帧len=%d , type=0x%02x", len, type);
    if(type != 0x0A){
        ESP_LOGW(TAG, "type=0x%02x 暂不处理", type);
        return ESP_ERR_NOT_SUPPORTED;
    }
    switch (type) {
        // case 0x00:
        
        //     break;
        // case 0x01:

        //     break;
        // case 0x02:

        //     break;
        // case 0x03:

        //     break;
        // case 0x04:

        //     break;
        // case 0x05:

        //     break;
        case 0x0A:
            yoke_rad60_process_multitarget_det_info(len - 1, buffer);
            return ESP_OK;
        default:
            ESP_LOGE(TAG, "Unknown type: 0x%02x", type);
            return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

//处理接收到的数据
esp_err_t yoke_rad60_process_data(uart_frame_t* frame) {
    if (frame == NULL) {
        ESP_LOGE(TAG, "frame is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    
    // 根据帧类型处理
    if (frame->frame_type == FRAME_TYPE_RESPONSE) {
        // 处理响应帧 (0x59)
        ret = yoke_rad60_process_response_frame(frame->response.cmd, frame->response.len, frame->response.buffer);
    } 
    else if (frame->frame_type == FRAME_TYPE_REPORT) {
        // 处理主动上报帧 (0x5A)
        // len表示TYPE+DATA的总长度，buffer存储的是TYPE字段后的数据，长度为 len-1
        ret = yoke_rad60_process_report_frame(frame->report.len, frame->report.type, frame->report.buffer);
    }
    else {
        ESP_LOGE(TAG, "未知帧类型: %d", frame->frame_type);
        ret = ESP_ERR_INVALID_ARG;
    }

    return ret;
}

/* ========================================= 任务函数 ========================================= */

/**
 * @brief 雷达接收处理任务
 *
 * 该任务负责持续处理UART队列中的所有响应和上报帧
 * 1. 从队列接收数据帧
 * 2. 调用处理函数更新缓存
 * 3. 通知UI任务（仅对上报帧）
 * 4. 超时时异步发送查询命令检测通信状态
 */
static void yoke_rad60_task(void* arg) {
    esp_err_t ret = ESP_OK;
    uart_frame_t uart_frame;
    uint32_t last_data_time_ms = xTaskGetTickCount();
    uint32_t last_query_time_ms = 0;
    bool query_sent = false;
    ESP_LOGI(TAG, "Radar receive task started");
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        // 持续处理队列中的所有响应和上报帧
        bool data_received = false;
        while (yoke_rad60_protocol_recv_frame(&uart_frame, 50)) {
            // 只处理上报帧（雷达数据），响应帧（命令响应）不触发RX灯
            bool is_report_frame = (uart_frame.frame_type == FRAME_TYPE_REPORT);
            
            data_received = true;
            ret = yoke_rad60_process_data(&uart_frame);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to process data: %d", ret);
            } else {
                // 只有成功处理上报帧（雷达数据）时才通知UI任务
                if (is_report_frame && radar_event_group != NULL) {
                    xEventGroupSetBits(radar_event_group, YOKE_RAD60_RX_DATA_RECEIVED_BIT);
                }
            }
            yoke_rad60_protocol_free_frame_buffer(&uart_frame);
        }
        
        // 更新最后收到数据的时间戳
        if (data_received) {
            last_data_time_ms = xTaskGetTickCount();
            query_sent = false;  // 收到数据，重置查询标志
            // 收到数据，标记通信正常
            if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                yoke_rad60_radar_data.is_comm_ok = true;
                xSemaphoreGive(radar_mutex);
            }
        }
        
        uint32_t current_time_ms = xTaskGetTickCount();
        
        // 若间隔500ms没有收到数据，更新为未检测到人
        if (current_time_ms - last_data_time_ms > 500) {
            if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                yoke_rad60_radar_data.is_detected = 0;
                yoke_rad60_radar_data.radar_info.obj_num = 0;
                xSemaphoreGive(radar_mutex);
            }
            
            // 每600ms发送一次查询命令（避免频繁查询）
            if (!query_sent && current_time_ms - last_query_time_ms > 600) {
                // 异步发送查询命令（不等待响应，避免死锁）
                yoke_rad60_protocol_write_bytes(YOKE_RAD60_CMD_GET_RADAR_STATE, NULL, 0);
                last_query_time_ms = current_time_ms;
                query_sent = true;
                ESP_LOGD(TAG, "发送雷达状态查询命令");
            }
            
            // 如果发送查询命令后1500ms还没收到任何响应（包括查询响应），标记通信异常
            // 但在初始化后5秒内不进行通信超时检测，给雷达足够的启动时间
            if (query_sent && current_time_ms - last_query_time_ms > 1500) {
                // 检查是否已经过了初始化宽容期(5秒)
                if (current_time_ms - radar_init_time_ms > 5000) {
                    if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        if (yoke_rad60_radar_data.is_comm_ok) {
                            ESP_LOGW(TAG, "雷达通信超时，标记为通信异常");
                            yoke_rad60_radar_data.is_comm_ok = false;
                        }
                        xSemaphoreGive(radar_mutex);
                    }
                    // 重置查询标志，允许继续发送查询命令来探测雷达是否恢复
                    query_sent = false;
                } else {
                    ESP_LOGD(TAG, "雷达初始化宽容期内，暂不判定通信异常");
                }
            }
            
            // 重置时间戳，避免重复更新
            last_data_time_ms = current_time_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ========================================= 初始化和注销函数 ========================================= */

/**
 * @brief 初始化雷达模块
 */
esp_err_t yoke_rad60_init(uint8_t uart_num, int16_t tx_pin, int16_t rx_pin, uint32_t boudrate) {
    ESP_LOGI(TAG, "Initializing YOKE_RAD60 radar module");

    // 检查是否已经初始化，如果已初始化，必须先调用deinit清理
    if (radar_mutex != NULL || cmd_send_mutex != NULL || yoke_rad60_task_handle != NULL || 
        radar_event_group != NULL || cmd_request.response_sem != NULL) {
        ESP_LOGE(TAG, "YOKE_RAD60 already initialized! Must call yoke_rad60_deinit() first");
        ESP_LOGE(TAG, "  radar_mutex=%p, cmd_send_mutex=%p, yoke_rad60_task=%p",
                 radar_mutex, cmd_send_mutex, yoke_rad60_task_handle);
        ESP_LOGE(TAG, "  radar_event_group=%p, response_sem=%p",
                 radar_event_group, cmd_request.response_sem);
        return ESP_ERR_INVALID_STATE;
    }

    // 创建互斥锁，用于保护雷达信息缓存
    radar_mutex = xSemaphoreCreateMutex();
    if (radar_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 创建命令发送互斥锁，用于保护命令发送和响应等待的完整过程
    cmd_send_mutex = xSemaphoreCreateMutex();
    if (cmd_send_mutex == NULL) {
        vSemaphoreDelete(radar_mutex);
        radar_mutex = NULL;
        ESP_LOGE(TAG, "Failed to create command send mutex");
        return ESP_ERR_NO_MEM;
    }

    // 创建响应信号量，用于同步等待响应
    cmd_request.response_sem = xSemaphoreCreateBinary();
    if (cmd_request.response_sem == NULL) {
        vSemaphoreDelete(radar_mutex);
        vSemaphoreDelete(cmd_send_mutex);
        radar_mutex = NULL;
        cmd_send_mutex = NULL;
        ESP_LOGE(TAG, "Failed to create response semaphore");
        return ESP_ERR_NO_MEM;
    }

    // 创建事件组，用于通知UI任务接收到数据
    radar_event_group = xEventGroupCreate();
    if (radar_event_group == NULL) {
        vSemaphoreDelete(radar_mutex);
        vSemaphoreDelete(cmd_send_mutex);
        vSemaphoreDelete(cmd_request.response_sem);
        radar_mutex = NULL;
        cmd_send_mutex = NULL;
        cmd_request.response_sem = NULL;
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    // 初始化命令请求结构
    cmd_request.cmd = 0;
    cmd_request.waiting = false;
    cmd_request.response_data = NULL;
    cmd_request.response_len = 0;
    cmd_request.result = ESP_OK;

    // 设置UART协议的日志级别
    esp_log_level_set("UART_Protocol", ESP_LOG_NONE);
    esp_log_level_set("yoke_rad60", ESP_LOG_NONE);

    // 记录初始化时间戳(用于给雷达启动留出宽容期)
    radar_init_time_ms = xTaskGetTickCount();
    
    // 重置通信状态为正常
    yoke_rad60_radar_data.is_comm_ok = true;

    // 初始化UART协议
    yoke_rad60_protocol_init(uart_num, tx_pin, rx_pin, boudrate);

    // 创建雷达接收处理任务
    BaseType_t task_ret = yoke_rad60_create_task(yoke_rad60_task, "yoke_rad60_task",
        YOKE_RAD60_TASK_STACK_SIZE, YOKE_RAD60_TASK_PRIORITY, &yoke_rad60_task_handle);
    if (task_ret != pdPASS) {
        vSemaphoreDelete(radar_mutex);
        vSemaphoreDelete(cmd_send_mutex);
        vSemaphoreDelete(cmd_request.response_sem);
        vEventGroupDelete(radar_event_group);
        yoke_rad60_protocol_deinit();
        radar_mutex = NULL;
        cmd_send_mutex = NULL;
        cmd_request.response_sem = NULL;
        radar_event_group = NULL;
        yoke_rad60_task_handle = NULL;
        ESP_LOGE(TAG, "Failed to create radar task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "YOKE_RAD60 radar module initialized successfully");
    return ESP_OK;
}

/**
 * @brief 注销雷达模块，释放资源
 */
esp_err_t yoke_rad60_deinit(void) {
    // 检查是否已初始化(如果所有关键资源都为NULL，说明未初始化)
    if (radar_mutex == NULL && cmd_send_mutex == NULL && yoke_rad60_task_handle == NULL) {
        ESP_LOGW(TAG, "YOKE_RAD60 not initialized, nothing to deinit");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing YOKE_RAD60 radar module");

    // Step 1: 删除雷达任务(yoke_rad60_task会调用yoke_rad60_protocol_recv_frame)
    // 必须先删除这个任务，否则它会继续尝试从队列读取数据
    if (yoke_rad60_task_handle != NULL) {
        ESP_LOGI(TAG, "Deleting YOKE_RAD60 radar task...");
        yoke_rad60_delete_task(yoke_rad60_task_handle);
        yoke_rad60_task_handle = NULL;
        ESP_LOGI(TAG, "YOKE_RAD60 radar task deleted");
    }
    
    // Step 2: 等待任务完全清理 - 增加延迟确保任务完全退出
    ESP_LOGI(TAG, "Waiting for radar task to clean up...");
    vTaskDelay(pdMS_TO_TICKS(250));

    // Step 3: 删除事件组
    if (radar_event_group != NULL) {
        vEventGroupDelete(radar_event_group);
        radar_event_group = NULL;
        ESP_LOGI(TAG, "Radar event group deleted");
    }

    // Step 4: 删除信号量
    if (cmd_request.response_sem != NULL) {
        vSemaphoreDelete(cmd_request.response_sem);
        cmd_request.response_sem = NULL;
        ESP_LOGI(TAG, "Radar response semaphore deleted");
    }

    // Step 5: 删除互斥锁
    if (radar_mutex != NULL) {
        vSemaphoreDelete(radar_mutex);
        radar_mutex = NULL;
        ESP_LOGI(TAG, "Radar mutex deleted");
    }

    // Step 6: 删除命令发送互斥锁
    if (cmd_send_mutex != NULL) {
        vSemaphoreDelete(cmd_send_mutex);
        cmd_send_mutex = NULL;
        ESP_LOGI(TAG, "Command send mutex deleted");
    }

    // Step 7: 最后释放UART协议资源(删除UART任务、队列和驱动)
    // 这个必须在yoke_rad60_task删除之后，因为yoke_rad60_task会使用UART队列
    yoke_rad60_protocol_deinit();

    // Step 8: 额外等待，确保UART驱动和所有资源完全清理
    ESP_LOGI(TAG, "Waiting for all resources to be fully released...");
    vTaskDelay(pdMS_TO_TICKS(250));

    ESP_LOGI(TAG, "YOKE_RAD60 radar module deinitialized");
    return ESP_OK;
}

/**
 * @brief 重新配置YOKE_RAD60雷达的UART引脚（不删除任务，轻量级切换）
 */
esp_err_t yoke_rad60_reconfigure_uart(int16_t tx_pin, int16_t rx_pin) {
    if (radar_mutex == NULL) {
        ESP_LOGE(TAG, "YOKE_RAD60 not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Reconfiguring YOKE_RAD60 UART pins: TX=%d, RX=%d", tx_pin, rx_pin);
    
    // 获取互斥锁
    if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take radar mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    // 调用UART协议层的重配置函数
    esp_err_t ret = yoke_rad60_protocol_reconfigure_pins(tx_pin, rx_pin);
    
    xSemaphoreGive(radar_mutex);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "YOKE_RAD60 UART pins reconfigured successfully");
        // 清空通信异常标志，给新配置一个机会
        yoke_rad60_radar_data.is_comm_ok = true;
    } else {
        ESP_LOGE(TAG, "Failed to reconfigure YOKE_RAD60 UART pins: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

/* ========================================= 公共API函数实现 ========================================= */

/* ========== 基本配置命令 ========== */

/**
 * @brief 设置感应电平持续时间
 */
esp_err_t yoke_rad60_set_sense_time(uint16_t sense_time) {
    uint8_t tx_data[2];
    yoke_rad60_pack_le16(tx_data, sense_time);
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_SENSE_TIME,
        tx_data, 2,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/**
 * @brief 获取感应电平持续时间
 */
esp_err_t yoke_rad60_get_sense_time(uint16_t* sense_time) {
    if (sense_time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_GET_SENSE_TIME,
        NULL, 0,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len >= 2) {
        *sense_time = yoke_rad60_unpack_le16(rx_data);
    }

    return ret;
}

/**
 * @brief 设置光敏阈值
 */
esp_err_t yoke_rad60_set_light_thres(uint16_t light_thres) {
    uint8_t tx_data[2];
    yoke_rad60_pack_le16(tx_data, light_thres);
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_LIGHT_THRES,
        tx_data, 2,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/**
 * @brief 获取光敏阈值
 */
esp_err_t yoke_rad60_get_light_thres(uint16_t* light_thres) {
    if (light_thres == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_GET_LIGHT_THRES,
        NULL, 0,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len >= 2) {
        *light_thres = yoke_rad60_unpack_le16(rx_data);
    }

    return ret;
}

/**
 * @brief 保存雷达设置到flash
 */
esp_err_t yoke_rad60_save_settings(void) {
    uint8_t tx_data[1] = { 0x01 };
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SAVE_SETTINGS,
        tx_data, 1,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/**
 * @brief 获取雷达保存状态
 */
esp_err_t yoke_rad60_get_save_status(uint8_t* status) {
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_GET_SAVE_STATUS,
        NULL, 0,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len >= 1) {
        *status = rx_data[0];
    }

    return ret;
}

/**
 * @brief 设置OUT信号状态
 */
esp_err_t yoke_rad60_set_out_state(uint8_t out_state) {
    uint8_t tx_data[1] = { out_state };
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_OUT_STATE,
        tx_data, 1,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/**
 * @brief 设置PWM占空比
 */
esp_err_t yoke_rad60_set_pwm_duty(uint8_t pwm_duty) {
    uint8_t tx_data[1] = { pwm_duty };
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_PWM_DUTY,
        tx_data, 1,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/**
 * @brief 系统复位
 */
esp_err_t yoke_rad60_system_reset(void) {
    uint8_t rx_data[8];

    return yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SYSTEM_RESET,
        NULL, 0,
        rx_data, NULL,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );
}

/**
 * @brief 波特率切换
 */
esp_err_t yoke_rad60_set_baud_rate(uint32_t baud_rate) {
    uint8_t tx_data[4];
    yoke_rad60_pack_le32(tx_data, baud_rate);
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_BAUD_RATE,
        tx_data, 4,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/* ========== 雷达配置命令 ========== */

/**
 * @brief 设置雷达感应等级
 */
esp_err_t yoke_rad60_set_sense_level(uint8_t level) {
    uint8_t tx_data[1] = { level };
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_SENSE_LEVEL,
        tx_data, 1,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/**
 * @brief 获取雷达感应等级
 */
esp_err_t yoke_rad60_get_sense_level(uint8_t* level) {
    if (level == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_GET_SENSE_LEVEL,
        NULL, 0,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len >= 1) {
        *level = rx_data[0];
    }

    return ret;
}

/**
 * @brief 获取雷达感应信息（从缓存读取，非阻塞）
 */
esp_err_t yoke_rad60_get_radar_info(fmcw_det_info_t* info) {
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memcpy(info, &radar_info, sizeof(fmcw_det_info_t));

    xSemaphoreGive(radar_mutex);

    return ESP_OK;
}

/**
 * @brief 获取当前算法类型
 */
esp_err_t yoke_rad60_get_algo_type(uint8_t* algo_type) {
    if (algo_type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_GET_ALGO_TYPE,
        NULL, 0,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len >= 1) {
        *algo_type = rx_data[0];
    }

    return ret;
}

/**
 * @brief 获取算法配置的边界值
 */
esp_err_t yoke_rad60_get_boundary(uint8_t* boundary) {
    if (boundary == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_GET_BOUNDARY,
        NULL, 0,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len >= 1) {
        *boundary = rx_data[0];
    }

    return ret;
}

/**
 * @brief 设置运动检测最近感应距离
 */
esp_err_t yoke_rad60_set_mot_near(uint16_t mot_near) {
    uint8_t tx_data[2];
    yoke_rad60_pack_le16(tx_data, mot_near);
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_MOT_NEAR,
        tx_data, 2,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/**
 * @brief 设置运动检测灵敏度
 */
esp_err_t yoke_rad60_set_mot_sens(uint8_t mot_sens) {
    uint8_t tx_data[1] = { mot_sens };
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_MOT_SENS,
        tx_data, 1,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/**
 * @brief 设置呼吸检测最远感应距离
 */
esp_err_t yoke_rad60_set_breath_far(uint16_t breath_far) {
    uint8_t tx_data[2];
    yoke_rad60_pack_le16(tx_data, breath_far);
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_BREATH_FAR,
        tx_data, 2,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/**
 * @brief 设置呼吸检测最近感应距离
 */
esp_err_t yoke_rad60_set_breath_near(uint16_t breath_near) {
    uint8_t tx_data[2];
    yoke_rad60_pack_le16(tx_data, breath_near);
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_BREATH_NEAR,
        tx_data, 2,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/**
 * @brief 设置呼吸检测灵敏度
 */
esp_err_t yoke_rad60_set_breath_sens(uint8_t breath_sens) {
    uint8_t tx_data[1] = { breath_sens };
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_BREATH_SENS,
        tx_data, 1,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/* ========== 雷达控制命令 ========== */

/**
 * @brief 获取雷达感应状态
 */
esp_err_t yoke_rad60_get_radar_state(uint8_t* state) {
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_GET_RADAR_STATE,
        NULL, 0,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len >= 1) {
        *state = rx_data[0];
    }

    return ret;
}

/**
 * @brief 设置雷达感应功能
 */
esp_err_t yoke_rad60_set_radar_enable(uint8_t enable) {
    uint8_t tx_data[1] = { enable };
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_RADAR_ENABLE,
        tx_data, 1,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/**
 * @brief 设置运动检测最远感应距离
 */
esp_err_t yoke_rad60_set_mot_far(uint16_t mot_far) {
    uint8_t tx_data[2];
    yoke_rad60_pack_le16(tx_data, mot_far);
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_MOT_FAR,
        tx_data, 2,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/* ================================== 超低功耗命令 =========================================== */

/**
 * @brief 进入/退出通信模式
 * @param enter 0:退出通信模式, 1:进入通信模式
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_enter_communication_mode(uint8_t enter) {
    uint8_t tx_data[1] = { enter };
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_ENTER_COMMUNICATION_MODE,
        tx_data, 1,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/**
 * @brief 设置超低功耗时间
 */
esp_err_t yoke_rad60_set_ulp_time(uint16_t ulp_time) {
    uint8_t tx_data[2];
    yoke_rad60_pack_le16(tx_data, ulp_time);
    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_SET_ULP_TIME,
        tx_data, 2,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

/**
 * @brief 读取AT5815C
 */
esp_err_t yoke_rad60_read_at5815c(uint8_t* data, uint32_t* len) {
    if (data == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rx_data[64];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_READ_AT5815C,
        NULL, 0,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        uint32_t copy_len = rx_len < *len ? rx_len : *len;
        memcpy(data, rx_data, copy_len);
        *len = copy_len;
    }

    return ret;
}

/**
 * @brief 写入AT5815C
 */
esp_err_t yoke_rad60_write_at5815c(const uint8_t* data, uint32_t len) {
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rx_data[8];
    uint32_t rx_len = 0;

    esp_err_t ret = yoke_rad60_send_cmd_sync(
        YOKE_RAD60_CMD_WRITE_AT5815C,
        data, len,
        rx_data, &rx_len,
        YOKE_RAD60_CMD_TIMEOUT_MS
    );

    if (ret == ESP_OK && rx_len > 0) {
        // 0x00 表示成功
        return (rx_data[0] == 0) ? ESP_OK : ESP_FAIL;
    }

    return ret;
}

//辅助函数:状态转字符串
char* det_status_to_string(uint8_t det_result) {
    switch(det_result) {
        case 0x01: return "靠近";
        case 0x02: return "远离";
        case 0x04: return "运动";
        case 0x08: return "微动";
        case 0x10: return "呼吸";
        default: return "未知";
    }
}

//设置雷达
void set_radar_config(void) {
    esp_err_t ret;
    //设置运动检测最近感应距离,单位cm
    uint16_t sense_distance = 10;
    ret = yoke_rad60_set_mot_near(sense_distance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置运动检测最近感应距离失败: %d", ret);
    } else {
        ESP_LOGI(TAG, "设置运动检测最近感应距离: %dcm", sense_distance);
    }

    //设置运动检测最远感应距离,单位cm
    uint16_t sense_max_distance = 1000;
    ret = yoke_rad60_set_mot_far(sense_max_distance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置运动检测最远感应距离失败: %d", ret);
    } else {
        ESP_LOGI(TAG, "设置运动检测最远感应距离: %dcm", sense_max_distance);
    }

    //设置运动检测灵敏度,参数范围：0 ~ 10
    uint8_t sense_sensitivity = 5;
    ret = yoke_rad60_set_mot_sens(sense_sensitivity);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置运动检测灵敏度失败: %d", ret);
    } else {
        ESP_LOGI(TAG, "设置运动检测灵敏度: %d", sense_sensitivity);
    }

    //设置呼吸检测最近感应距离,单位cm
    uint16_t breath_near_distance = 10;
    ret = yoke_rad60_set_breath_near(breath_near_distance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置呼吸检测最近感应距离失败: %d", ret);
    } else {
        ESP_LOGI(TAG, "设置呼吸检测最近感应距离: %dcm", breath_near_distance);
    }

    //设置呼吸检测最远感应距离,单位cm
    uint16_t micro_max_distance = 1000;
    ret = yoke_rad60_set_breath_far(micro_max_distance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置呼吸检测最远感应距离失败: %d", ret);
    } else {
        ESP_LOGI(TAG, "设置呼吸检测最远感应距离: %dcm", micro_max_distance);
    }

    //设置呼吸检测灵敏度,参数范围：0 ~ 10
    uint8_t breath_sensitivity = 5;
    ret = yoke_rad60_set_breath_sens(breath_sensitivity);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置呼吸检测灵敏度失败: %d", ret);
    } else {
        ESP_LOGI(TAG, "设置呼吸检测灵敏度: %d", breath_sensitivity);
    }

    //获取雷达感应状态,0x01=开启,0x00=关闭
    uint8_t radar_state = 0;
    ret = yoke_rad60_get_radar_state(&radar_state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "获取雷达感应状态失败: %d", ret);
    } else {
        ESP_LOGI(TAG, "雷达感应状态: %s", radar_state==0x01?"开启":"关闭");
    }
}

/**
 * @brief 获取雷达数据（非阻塞）
 * @param data 输出参数，雷达数据
 * @return esp_err_t ESP_OK成功, 其他失败
 */
esp_err_t yoke_rad60_get_radar_data(yoke_rad60_radar_data_t* data){
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if(radar_mutex == NULL){
        ESP_LOGE(TAG, "雷达互斥锁未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memcpy(data, &yoke_rad60_radar_data, sizeof(yoke_rad60_radar_data_t));

    xSemaphoreGive(radar_mutex);

    // 通信异常时返回错误码，但数据已经填充（包括正确的 is_comm_ok 状态）
    if(yoke_rad60_radar_data.is_comm_ok == false){
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}
