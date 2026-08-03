#pragma once

#include <stdint.h>
#include "driver/uart.h"

#define UART_NUM UART_NUM_1
#define UART_QUEUE_LENGTH 5
#define UART_SEND_QUEUE_LENGTH 10  // 发送队列深度

#define RX_BUF_SIZE 2048
#define TX_BUF_SIZE 1024
#define RX_FRAME_MAX_SIZE (2 * 1024)

// AT6010协议定义
#define PACK_SEND_HEAD 0x58      // 发送帧头
#define PACK_RECV_HEAD 0x59      // 接收帧头（响应帧）
#define PACK_REPORT_HEAD 0x5A    // 接收帧头（主动上报帧）
#define YOKE_RAD60_FRAME_MIN_LEN 5   // 最小帧长度：Head(1) + CMD(1) + PayloadLen(1) + CheckCode(2)

// 帧类型枚举
typedef enum {
    FRAME_TYPE_RESPONSE = 0x59,  // 响应帧 (0x59)
    FRAME_TYPE_REPORT = 0x5A     // 主动上报帧 (0x5A)
} uart_frame_type_e;

// 响应帧结构体 (0x59)
typedef struct {
    uint8_t head;    // 帧头 (0x59)
    uint8_t cmd;     // 命令字
    uint8_t len;     // 数据长度
    uint8_t* buffer; // 数据指针
} uart_response_frame_t;

// 主动上报帧结构体 (0x5A)
typedef struct {
    uint8_t head;    // 帧头 (0x5A)
    uint8_t len;     // TYPE+DATA的总长度
    uint8_t type;    // 类型字段
    uint8_t* buffer; // 数据指针
} uart_report_frame_t;

// 统一的帧结构体（使用联合体）
typedef struct {
    uart_frame_type_e frame_type;  // 帧类型标识
    union {
        uart_response_frame_t response;  // 响应帧数据
        uart_report_frame_t report;      // 主动上报帧数据
    };
} uart_frame_t;

// 发送帧结构体
typedef struct {
    uint8_t* data;
    uint16_t len;
} uart_send_msg_t;

#ifdef __cplusplus
extern "C" {
#endif

void yoke_rad60_protocol_init(uint8_t uart_num, int16_t tx_pin, int16_t rx_pin, uint32_t boudrate);

void yoke_rad60_protocol_deinit(void);

void yoke_rad60_protocol_write_bytes(uint16_t cmd, const uint8_t* frame, uint32_t len);

void yoke_rad60_protocol_write_raw_bytes(const uint8_t* data, uint32_t len);

void yoke_rad60_protocol_respond_bytes(uint16_t cmd, const uint8_t* value, uint32_t len, uint8_t ack, uint8_t finish);

void yoke_rad60_protocol_respond_byte(uint16_t cmd, const uint8_t status, uint8_t ack, uint8_t finish);

int yoke_rad60_protocol_recv_frame(uart_frame_t* frame, uint32_t timeout_ms);

void yoke_rad60_protocol_free_frame_buffer(uart_frame_t* frame);

/**
 * @brief 重新配置UART引脚（不删除任务和队列）
 * @param tx_pin 新的TX引脚
 * @param rx_pin 新的RX引脚
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t yoke_rad60_protocol_reconfigure_pins(int16_t tx_pin, int16_t rx_pin);

#ifdef __cplusplus
}
#endif


