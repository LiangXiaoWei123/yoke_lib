#pragma once

#include <stdint.h>
#include "driver/uart.h"

#define UART_NUM UART_NUM_1
#define UART_QUEUE_LENGTH 5

#define RX_BUF_SIZE 2048
#define TX_BUF_SIZE 1024
#define RX_FRAME_MAX_SIZE (2 * 1024)

#define PACK_SEND_HEAD 0x58
#define PACK_RECV_HEAD 0x59
#define PACK_REPORT_HEAD 0x5A
#define YOKE_RAD60_FRAME_MIN_LEN 5

typedef enum {
    FRAME_TYPE_RESPONSE = 0x59,
    FRAME_TYPE_REPORT = 0x5A,
} uart_frame_type_e;

typedef struct {
    uint8_t head;
    uint8_t cmd;
    uint8_t len;
    uint8_t* buffer;
} uart_response_frame_t;

typedef struct {
    uint8_t head;
    uint8_t len;
    uint8_t type;
    uint8_t* buffer;
} uart_report_frame_t;

typedef struct {
    uart_frame_type_e frame_type;
    union {
        uart_response_frame_t response;
        uart_report_frame_t report;
    };
} uart_frame_t;

#ifdef __cplusplus
extern "C" {
#endif

void yoke_rad60_protocol_init(uint8_t uart_num, int16_t tx_pin, int16_t rx_pin, uint32_t boudrate);
void yoke_rad60_protocol_deinit(void);
void yoke_rad60_protocol_write_bytes(uint16_t cmd, const uint8_t* frame, uint32_t len);
int yoke_rad60_protocol_recv_frame(uart_frame_t* frame, uint32_t timeout_ms);
void yoke_rad60_protocol_free_frame_buffer(uart_frame_t* frame);

#ifdef __cplusplus
}
#endif
