/** @file yoke_rad60_protocol.c */
#include "yoke_rad60_protocol.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
static BaseType_t yoke_rad60_protocol_create_task(
    TaskFunction_t function, const char *name, uint32_t stack_size, void *argument,
    UBaseType_t priority, TaskHandle_t *handle, BaseType_t core_id, bool stack_in_psram)
{
    (void)core_id;
    (void)stack_in_psram;
    return xTaskCreate(function, name, stack_size, argument, priority, handle);
}
#include "sdkconfig.h"
#include "string.h"

#define TAG "UART_Protocol"

static uint8_t g_uart_num = 0xFF;  // 初始化为无效值，防止误删除其他UART
static QueueHandle_t uart_queue = NULL;
static QueueHandle_t uart_buffer_queue = NULL;
static SemaphoreHandle_t uart_semaphore = NULL;

static QueueHandle_t uart_send_queue = NULL;
static TaskHandle_t uart_frame_task_handle = NULL;
static TaskHandle_t uart_send_task_handle = NULL;

// Protocol
static void uart_frame_task(void* arg); // 接收数据任务
static void uart_send_task(void* arg); // 发送数据任务

void yoke_rad60_protocol_init(uint8_t uart_num, int16_t tx_pin, int16_t rx_pin, uint32_t boudrate) {
    // 安全检查: 如果已经初始化过，必须先调用yoke_rad60_protocol_deinit清理
    if (uart_semaphore != NULL || uart_buffer_queue != NULL || uart_send_queue != NULL || 
        uart_frame_task_handle != NULL || uart_send_task_handle != NULL || g_uart_num != 0xFF) {
        ESP_LOGE(TAG, "UART protocol already initialized! Must call yoke_rad60_protocol_deinit() first");
        ESP_LOGE(TAG, "  uart_semaphore=%p, uart_buffer_queue=%p, uart_send_queue=%p", 
                 uart_semaphore, uart_buffer_queue, uart_send_queue);
        ESP_LOGE(TAG, "  uart_frame_task=%p, uart_send_task=%p, g_uart_num=%d",
                 uart_frame_task_handle, uart_send_task_handle, g_uart_num);
        // 不在这里调用deinit，因为可能在任务上下文中调用，会导致栈问题
        // 调用者必须先调用deinit，等待一段时间后再调用init
        return;
    }
    
    uart_semaphore = xSemaphoreCreateMutex();
    uart_buffer_queue = xQueueCreate(UART_QUEUE_LENGTH, sizeof(uart_frame_t));
    uart_send_queue = xQueueCreate(UART_SEND_QUEUE_LENGTH, sizeof(uart_send_msg_t));
    const uart_config_t uart_config = {
        .baud_rate = boudrate, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
    g_uart_num = uart_num;
    uart_param_config(g_uart_num, &uart_config);
    uart_set_pin(g_uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(g_uart_num, RX_BUF_SIZE, TX_BUF_SIZE, UART_QUEUE_LENGTH, &uart_queue, ESP_INTR_FLAG_IRAM);
    uart_set_rx_timeout(g_uart_num, 50);
    if (boudrate > 460800) {
        uart_set_rx_full_threshold(g_uart_num, 50);
    }
    yoke_rad60_protocol_create_task(uart_frame_task, "uart_queue_task", 3 * 1024, NULL, 10, &uart_frame_task_handle, 1, 1);
    yoke_rad60_protocol_create_task(uart_send_task, "uart_send_task", 2 * 1024, NULL, 10, &uart_send_task_handle, 1, 1);
}

void yoke_rad60_protocol_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing UART protocol");
    
    bool has_tasks = false;
    
    // Step 1: 删除UART发送任务
    if (uart_send_task_handle != NULL) {
        ESP_LOGI(TAG, "Deleting UART send task (stack=2KB)...");

        #if (configSUPPORT_STATIC_ALLOCATION == 1 && CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY == 1)
        vTaskDeleteWithCaps(uart_send_task_handle);
        #else
        vTaskDelete(uart_send_task_handle);
        #endif
        uart_send_task_handle = NULL;
        has_tasks = true;
        ESP_LOGI(TAG, "UART send task deleted");
    }
    
    // Step 2: 删除UART接收任务
    if (uart_frame_task_handle != NULL) {
        ESP_LOGI(TAG, "Deleting UART frame task (stack=3KB)...");

        #if (configSUPPORT_STATIC_ALLOCATION == 1 && CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY == 1)
        vTaskDeleteWithCaps(uart_frame_task_handle);
        #else
        vTaskDelete(uart_frame_task_handle);
        #endif
        uart_frame_task_handle = NULL;
        has_tasks = true;
        ESP_LOGI(TAG, "UART frame task deleted");
    }
    
    // Step 3: 等待任务完全退出 - 增加延迟确保任务栈完全清理
    if (has_tasks) {
        ESP_LOGI(TAG, "Waiting for UART tasks to clean up...");
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    // Step 4: 清空并删除发送队列，释放队列中待发送数据的内存
    if (uart_send_queue != NULL) {
        uart_send_msg_t msg;
        uint32_t freed_count = 0;
        // 从队列中取出所有待发送消息并释放其内存
        while (xQueueReceive(uart_send_queue, &msg, 0) == pdTRUE) {
            if (msg.data != NULL) {
                free(msg.data);
                freed_count++;
            }
        }
        if (freed_count > 0) {
            ESP_LOGI(TAG, "Freed %" PRIu32 " pending send buffers from uart_send_queue", freed_count);
        }
        vQueueDelete(uart_send_queue);
        uart_send_queue = NULL;
        ESP_LOGI(TAG, "UART send queue deleted");
    }
    
    // Step 5: 清空并删除接收缓冲队列，释放队列中帧的buffer内存
    if (uart_buffer_queue != NULL) {
        uart_frame_t frame;
        uint32_t freed_count = 0;
        // 从队列中取出所有帧并释放其buffer内存
        while (xQueueReceive(uart_buffer_queue, &frame, 0) == pdTRUE) {
            yoke_rad60_protocol_free_frame_buffer(&frame);
            freed_count++;
        }
        if (freed_count > 0) {
            ESP_LOGI(TAG, "Freed %" PRIu32 " pending frame buffers from uart_buffer_queue", freed_count);
        }
        vQueueDelete(uart_buffer_queue);
        uart_buffer_queue = NULL;
        ESP_LOGI(TAG, "UART buffer queue deleted");
    }
    
    // Step 6: 删除信号量
    if (uart_semaphore != NULL) {
        vSemaphoreDelete(uart_semaphore);
        uart_semaphore = NULL;
        ESP_LOGI(TAG, "UART semaphore deleted");
    }
    
    // Step 7: 卸载UART驱动（会自动释放uart_queue和RX/TX缓冲区）
    if (g_uart_num != 0xFF) {
        ESP_LOGI(TAG, "Deleting UART driver for UART%d...", g_uart_num);
        esp_err_t err = uart_driver_delete(g_uart_num);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "UART driver deleted successfully");
        } else {
            ESP_LOGE(TAG, "Failed to delete UART driver: %s", esp_err_to_name(err));
        }
        // uart_queue由uart_driver_delete自动释放，不需要手动删除
        //如果uart_queue没有释放，则需要手动删除
        if (uart_queue != NULL) {
            vQueueDelete(uart_queue);
        }
        uart_queue = NULL;
        // 重置UART编号为无效值，防止误操作
        g_uart_num = 0xFF;
    }
    
    // Step 8: 额外延迟，确保UART驱动完全清理
    vTaskDelay(pdMS_TO_TICKS(50));
    
    ESP_LOGI(TAG, "UART protocol deinitialized");
}

/**
 * @brief 重新配置UART引脚（不删除任务和队列，轻量级切换）
 * @param tx_pin 新的TX引脚
 * @param rx_pin 新的RX引脚
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t yoke_rad60_protocol_reconfigure_pins(int16_t tx_pin, int16_t rx_pin) {
    if (g_uart_num == 0xFF) {
        ESP_LOGE(TAG, "UART not initialized, cannot reconfigure pins");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Reconfiguring UART%d pins: TX=%d, RX=%d", g_uart_num, tx_pin, rx_pin);
    
    // 获取信号量保护
    if (uart_semaphore == NULL) {
        ESP_LOGE(TAG, "UART semaphore is NULL");
        return ESP_ERR_INVALID_STATE;
    }
    
    xSemaphoreTake(uart_semaphore, portMAX_DELAY);
    
    // 1. 刷新UART缓冲区
    uart_flush(g_uart_num);
    
    // 2. 等待UART发送完成
    uart_wait_tx_done(g_uart_num, pdMS_TO_TICKS(100));
    
    // 3. 重新设置引脚
    esp_err_t ret = uart_set_pin(g_uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    
    xSemaphoreGive(uart_semaphore);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "UART pins reconfigured successfully");
    } else {
        ESP_LOGE(TAG, "Failed to reconfigure UART pins: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

// Yoke-RAD60 协议：Head(0x58) + CMD(1) + ParaLen(1) + Para_0~n + CheckCode(2)
void yoke_rad60_protocol_write_bytes(uint16_t cmd, const uint8_t* frame, uint32_t len) {
    xSemaphoreTake(uart_semaphore, pdMS_TO_TICKS(10));
    
    // 计算总帧长度：Head(1) + CMD(1) + ParaLen(1) + Parameters(len) + CheckCode(2)
    uint32_t total_len = 1 + 1 + 1 + len + 2;
    uint8_t* out_buf = (uint8_t*)malloc(total_len);
    if (out_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate send buffer");
        xSemaphoreGive(uart_semaphore);
        return;
    }
    
    // 构建帧
    out_buf[0] = PACK_SEND_HEAD;         // Head: 0x58
    out_buf[1] = cmd & 0xFF;             // CMD (只取低8位)
    out_buf[2] = len & 0xFF;             // Parameter Length
    
    // 复制参数数据
    if (len > 0 && frame != NULL) {
        memcpy(&out_buf[3], frame, len);
    }
    
    // 计算Check Code：所有数据的和（不包括CheckCode本身）
    uint16_t check_sum = 0;
    for (uint32_t i = 0; i < 3 + len; i++) {
        check_sum += out_buf[i];
    }
    
    // 添加Check Code（小端序：低字节在前）
    out_buf[3 + len] = check_sum & 0xFF;              // 低字节
    out_buf[3 + len + 1] = (check_sum >> 8) & 0xFF;   // 高字节
    
    // 打印发送的完整帧
    ESP_LOGI(TAG,"====发送数据====");
    if (total_len <= 32) {
        ESP_LOG_BUFFER_HEX(TAG, out_buf, total_len);
    } else {
        ESP_LOG_BUFFER_HEX(TAG, out_buf, 32);
        ESP_LOGI(TAG, "... (%" PRIu32 " more bytes)", total_len - 32);
    }
    // 发送
    // uart_write_bytes(g_uart_num, out_buf, total_len);
    uart_send_msg_t msg = { .data = out_buf, .len = total_len };
    
    if (xQueueSend(uart_send_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "UART send queue full, frame dropped (cmd=0x%02x)",cmd);
        free(out_buf);  // 队列满，释放内存
    }
    
    xSemaphoreGive(uart_semaphore);
}

/**
 * @brief UART 异步发送任务
 *
 * 从队列中取出完整的帧，一次性写入 UART，保证帧完整性
 */
static void uart_send_task(void* arg) {
    uart_send_msg_t msg;
    ESP_LOGI(TAG, "UART send task started");

    while (1) {
        // 从队列接收消息
        if (xQueueReceive(uart_send_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.data && msg.len > 0) {
                // ✅ 一次性写入完整帧（原子操作，保证帧完整性）
                int written = uart_write_bytes(g_uart_num, msg.data, msg.len);

                if (written != msg.len) {
                    ESP_LOGW(TAG, "UART write incomplete: %d/%d bytes", written, msg.len);
                }

                // 释放内存
                free(msg.data);
            } else {
                ESP_LOGE(TAG, "Invalid UART send message");
            }
        }
    }
}

//发送原始数据 不进行协议封装 直接发送 测试使用
void yoke_rad60_protocol_write_raw_bytes(const uint8_t* data, uint32_t len) {
    if (data == NULL || len == 0) {
        ESP_LOGW(TAG, "Invalid raw data: data=%p, len=%" PRIu32, data, len);
        return;
    }
    xSemaphoreTake(uart_semaphore, pdMS_TO_TICKS(10));
    ESP_LOGD(TAG, "Out Raw -> %" PRIu32 " bytes", len);
    if (len < 16) {
        ESP_LOG_BUFFER_HEX(TAG, data, len);
    } else {
        ESP_LOG_BUFFER_HEX(TAG, data, 16);
    }
    uart_write_bytes(g_uart_num, data, len);
    xSemaphoreGive(uart_semaphore);
}

void yoke_rad60_protocol_respond_bytes(uint16_t cmd, const uint8_t* value, uint32_t len, uint8_t ack, uint8_t finish) {
    if (ack) {
        cmd = cmd | 0x8000;
    }
    if (finish) {
        cmd = cmd | 0x4000;
    }
    yoke_rad60_protocol_write_bytes(cmd, value, len);
}

void yoke_rad60_protocol_respond_byte(uint16_t cmd, uint8_t status, uint8_t ack, uint8_t finish) {
    yoke_rad60_protocol_respond_bytes(cmd, &status, 1, ack, finish);
}

int yoke_rad60_protocol_recv_frame(uart_frame_t* frame, uint32_t timeout_ms) {
    if (frame == NULL) {
        return 0;
    }
    // 增加安全检查: 队列可能在deinit时已被删除
    if (uart_buffer_queue == NULL) {
        return 0;
    }
    if (timeout_ms != portMAX_DELAY) {
        timeout_ms = pdMS_TO_TICKS(timeout_ms);
    }
    int result = xQueueReceive(uart_buffer_queue, frame, timeout_ms);
    return result;
}

void yoke_rad60_protocol_free_frame_buffer(uart_frame_t* frame) {
    if (frame == NULL) {
        return;
    }
    
    // 根据帧类型释放对应的buffer
    if (frame->frame_type == FRAME_TYPE_RESPONSE) {
        if (frame->response.buffer) {
            free(frame->response.buffer);
            frame->response.buffer = NULL;
        }
    } else if (frame->frame_type == FRAME_TYPE_REPORT) {
        if (frame->report.buffer) {
            free(frame->report.buffer);
            frame->report.buffer = NULL;
        }
    }
}

//响应帧回调函数 (0x59)
void __attribute__((weak)) frame_recv_callback(uint8_t frame_head, int cmd, const uint8_t* buf, int len) {
    ESP_LOGD(TAG, "Response <- cmd=0x%02x, len=%d", cmd, len);
    
    uart_frame_t frame;
    frame.frame_type = FRAME_TYPE_RESPONSE;
    frame.response.head = frame_head;
    frame.response.cmd = cmd;
    frame.response.len = len;
    
    if (len > 0) {
        frame.response.buffer = (uint8_t*)malloc(len);
        if (frame.response.buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate response frame buffer (%d bytes) for cmd 0x%x", len, cmd);
            return;
        }
        memcpy(frame.response.buffer, buf, len);
    } else {
        frame.response.buffer = NULL;
    }
    
    if (xQueueSend(uart_buffer_queue, &frame, pdMS_TO_TICKS(10)) == pdFAIL) {
        ESP_LOGE(TAG, "Deal recv response frame too slow, cmd=0x%02x", cmd);
        if (frame.response.buffer) {
            free(frame.response.buffer);
        }
    }
}

//主动上报帧回调函数 (0x5A)
void __attribute__((weak)) frame_recv_report_callback(uint8_t frame_head, uint8_t len, uint8_t type, const uint8_t* buf) {
    ESP_LOGD(TAG, "Report <- type=%d, data_len=%d", type, len - 1);
    
    uart_frame_t frame;
    frame.frame_type = FRAME_TYPE_REPORT;
    frame.report.head = frame_head;
    frame.report.len = len;
    frame.report.type = type;
    
    // 计算实际数据长度 (len包含TYPE字段，这里存储的是TYPE后的数据)
    uint8_t data_len = (len > 1) ? (len - 1) : 0;
    
    if (data_len > 0) {
        frame.report.buffer = (uint8_t*)malloc(data_len);
        if (frame.report.buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate report frame buffer (%d bytes) for type=%d", data_len, type);
            return;
        }
        memcpy(frame.report.buffer, buf, data_len);
    } else {
        frame.report.buffer = NULL;
    }
    
    if (xQueueSend(uart_buffer_queue, &frame, pdMS_TO_TICKS(10)) == pdFAIL) {
        ESP_LOGE(TAG, "Deal recv report frame too slow, type=%d", type);
        if (frame.report.buffer) {
            free(frame.report.buffer);
        }
    }
}

// Yoke-RAD60 协议接收解析：
// 0x59响应帧: Head(0x59) + CMD(1) + ParaLen(1) + Para_0~n + CheckCode(2)
// 0x5A主动上报帧: Head(0x5A) + LEN(1) + TYPE(1) + DATA(LEN-1) + CHECK(1)
static int32_t deal_uart_buffer(uint8_t* buffer, uint32_t data_length) {
    uint8_t* deal_ptr = (uint8_t*)buffer;
    uint32_t total_processed = 0;

    // 支持在一次调用中处理多个帧
    while (data_length > 0) {
        uint8_t frame_head = 0;
        
        // 搜索帧头 0x59（响应帧）或 0x5A（主动上报帧）
        while (data_length > 0) {
            if (deal_ptr[0] == PACK_RECV_HEAD || deal_ptr[0] == PACK_REPORT_HEAD) {
                frame_head = deal_ptr[0];  // 保存找到的帧头类型
                break;
            }
            deal_ptr += 1;
            data_length -= 1;
            total_processed += 1;
        }

        // 检查是否还有足够数据读取最小帧
        if (data_length < YOKE_RAD60_FRAME_MIN_LEN) {
            goto exit;
        }

        // 根据帧头类型处理不同的帧格式
        if (frame_head == PACK_RECV_HEAD) {
            // 0x59 响应帧处理: Head + CMD + ParaLen + Para + CheckCode(2)
            uint8_t cmd = deal_ptr[1];
            uint8_t para_len = deal_ptr[2];
            
            // 计算完整帧长度：Head(1) + CMD(1) + ParaLen(1) + Parameters(para_len) + CheckCode(2)
            uint32_t packet_len = 1 + 1 + 1 + para_len + 2;
            
            // 验证帧长度合理性
            if (packet_len > RX_FRAME_MAX_SIZE) {
                ESP_LOGE(TAG, "0x59 frame: Invalid parameter length: %d (frame would be %" PRIu32 " bytes)", 
                         para_len, packet_len);
                // 跳过错误的帧头，继续搜索
                deal_ptr += 1;
                data_length -= 1;
                total_processed += 1;
                continue;
            }

            // 检查是否有完整帧数据
            if (data_length < packet_len) {
                goto exit;
            }

            // 校验Check Code：计算Head到最后一个参数的和
            uint16_t check_sum_calc = 0;
            for (uint32_t i = 0; i < 3 + para_len; i++) {
                check_sum_calc += deal_ptr[i];
            }
            
            // 读取接收到的Check Code（小端序：低字节在前）
            uint16_t check_sum_recv = deal_ptr[3 + para_len] | (deal_ptr[3 + para_len + 1] << 8);
            
            if (check_sum_calc != check_sum_recv) {
                ESP_LOGE(TAG, "0x59 frame: Check Code error: expected=0x%04x, received=0x%04x, cmd=0x%02x, para_len=%d", 
                         check_sum_calc, check_sum_recv, cmd, para_len);
                // 跳过错误帧，继续处理
                deal_ptr += 1;
                data_length -= 1;
                total_processed += 1;
                continue;
            }

            // 回调处理有效帧（传递帧头、CMD和参数部分）
            frame_recv_callback(frame_head, cmd, &deal_ptr[3], para_len);

            // 移动到下一帧
            deal_ptr += packet_len;
            data_length -= packet_len;
            total_processed += packet_len;
            
        } else if (frame_head == PACK_REPORT_HEAD) {
            // 0x5A 主动上报帧处理: Head + LEN + TYPE + DATA + CHECK(1)
            uint8_t len = deal_ptr[1];  // LEN字段包含TYPE+DATA的长度
            
            // 计算完整帧长度：Head(1) + LEN(1) + Payload(len) + CHECK(1)
            uint32_t packet_len = 1 + 1 + len + 1;
            
            // 验证帧长度合理性（len至少为1，因为要包含TYPE字段）
            if (len < 1 || packet_len > RX_FRAME_MAX_SIZE) {
                ESP_LOGE(TAG, "0x5A frame: Invalid LEN: %d (frame would be %" PRIu32 " bytes)", 
                         len, packet_len);
                // 跳过错误的帧头，继续搜索
                deal_ptr += 1;
                data_length -= 1;
                total_processed += 1;
                continue;
            }

            // 检查是否有完整帧数据
            if (data_length < packet_len) {
                goto exit;
            }

            // 校验CHECK：从HEAD到最后一个数据字节的累加和（取低8位）
            uint8_t check_sum_calc = 0;
            for (uint32_t i = 0; i < 2 + len; i++) {
                check_sum_calc += deal_ptr[i];
            }
            
            // 读取接收到的CHECK（1字节）
            uint8_t check_sum_recv = deal_ptr[2 + len];
            
            if (check_sum_calc != check_sum_recv) {
                uint8_t type = deal_ptr[2];
                ESP_LOGE(TAG, "0x5A frame: CHECK error: expected=0x%02x, received=0x%02x, len=%d, type=%d", 
                         check_sum_calc, check_sum_recv, len, type);
                // 跳过错误帧，继续处理
                deal_ptr += 1;
                data_length -= 1;
                total_processed += 1;
                continue;
            }

            // 提取TYPE字段和DATA部分
            uint8_t type = deal_ptr[2];
            uint8_t* data_ptr = &deal_ptr[3];  // TYPE后面的数据
            
            // 回调处理有效帧（传递帧头、TYPE和数据部分）
            frame_recv_report_callback(frame_head, len, type, data_ptr);

            // 移动到下一帧
            deal_ptr += packet_len;
            data_length -= packet_len;
            total_processed += packet_len;
        }
    }

exit:
    return total_processed;
}

static void uart_frame_task(void* arg) {
    uart_event_t xEvent;
    // 使用静态缓冲区，避免在任务删除时内存泄漏
    static uint8_t rx_buf[RX_FRAME_MAX_SIZE + 5];
    ESP_LOGI(TAG, "rx_buf allocated successfully (%" PRIu32 " bytes) at %p", (uint32_t)(RX_FRAME_MAX_SIZE + 5), rx_buf);
    uint32_t buffer_total_len = 0;
    uint32_t last_update_ticks = 0;
    for (;;) {
        if (xQueueReceive(uart_queue, (void*)&xEvent, portMAX_DELAY) == pdTRUE) {
            switch (xEvent.type) {
                case UART_DATA: {
                    size_t event_read_size;
                    uint32_t need_read_size;
                    uint32_t used_size;
                    uart_get_buffered_data_len(g_uart_num, &event_read_size);
                    if (event_read_size == 0) {
                        continue;
                    }

                    if (xTaskGetTickCount() - last_update_ticks > pdMS_TO_TICKS(50)) {
                        buffer_total_len = 0;
                    }

                    do {
                        last_update_ticks = xTaskGetTickCount();
                        // ESP_LOGD(TAG, "%d - %" PRIu32, event_read_size, buffer_total_len + event_read_size);

                        // 优化缓冲区空间计算
                        if (buffer_total_len + event_read_size > RX_FRAME_MAX_SIZE) {
                            need_read_size = RX_FRAME_MAX_SIZE - buffer_total_len;
                            if (need_read_size == 0) {
                                ESP_LOGW(TAG, "Buffer full, discarding old data");
                                buffer_total_len = 0;
                                need_read_size = (event_read_size > RX_FRAME_MAX_SIZE) ? RX_FRAME_MAX_SIZE : event_read_size;
                            }
                        } else {
                            need_read_size = event_read_size;
                        }

                        uart_read_bytes(g_uart_num, rx_buf + buffer_total_len, need_read_size, portMAX_DELAY);
                        
                        // 打印刚接收到的原始数据
                        ESP_LOGI(TAG, "====接收数据==== (%d bytes):", need_read_size);
                        if (need_read_size <= 32) {
                            ESP_LOG_BUFFER_HEX(TAG, rx_buf + buffer_total_len, need_read_size);
                        } else {
                            ESP_LOG_BUFFER_HEX(TAG, rx_buf + buffer_total_len, 32);
                            ESP_LOGI(TAG, "... (%d more bytes)", need_read_size - 32);
                        }

                        buffer_total_len += need_read_size;

                        used_size = deal_uart_buffer(rx_buf, buffer_total_len);
                        if (used_size > 0) {
                            buffer_total_len -= used_size;
                            // 只有在必要时才进行内存移动
                            if (buffer_total_len > 0 && used_size > 0) {
                                memmove(rx_buf, rx_buf + used_size, buffer_total_len);
                            }
                        }

                        uart_get_buffered_data_len(g_uart_num, &event_read_size);
                    } while ((buffer_total_len > 1 && used_size > 0) || event_read_size > 0);
                    break;
                }
                case UART_FIFO_OVF:
                    ESP_LOGE(TAG, "UART FIFO OVF");
                    break;
                case UART_BUFFER_FULL:
                    ESP_LOGE(TAG, "UART BUFFER FULL");
                    uart_flush_input(g_uart_num);
                    xQueueReset(uart_queue);
                    break;
                case UART_BREAK:
                    break;
                case UART_PARITY_ERR:
                    ESP_LOGE(TAG, "UART PARITY ERR");
                    break;
                case UART_FRAME_ERR:
                    ESP_LOGE(TAG, "UART FRAME ERR");
                    break;
                default:
                    break;
            }
        }
    }
    vTaskDelete(NULL);
}

