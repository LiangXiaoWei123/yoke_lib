/** @file yoke_rad60_protocol.c */
#include "yoke_rad60_protocol.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#define TAG "UART_Protocol"

static uint8_t g_uart_num = 0xFF;
static QueueHandle_t uart_queue = NULL;
static QueueHandle_t uart_buffer_queue = NULL;
static SemaphoreHandle_t uart_semaphore = NULL;
static TaskHandle_t uart_frame_task_handle = NULL;

static void uart_frame_task(void* arg);

void yoke_rad60_protocol_init(uint8_t uart_num, int16_t tx_pin, int16_t rx_pin, uint32_t boudrate) {
    if (uart_semaphore != NULL || uart_buffer_queue != NULL || uart_frame_task_handle != NULL || g_uart_num != 0xFF) {
        ESP_LOGE(TAG, "UART protocol already initialized");
        return;
    }

    uart_semaphore = xSemaphoreCreateMutex();
    uart_buffer_queue = xQueueCreate(UART_QUEUE_LENGTH, sizeof(uart_frame_t));
    const uart_config_t uart_config = { .baud_rate = boudrate,
                                        .data_bits = UART_DATA_8_BITS,
                                        .parity = UART_PARITY_DISABLE,
                                        .stop_bits = UART_STOP_BITS_1,
                                        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE };
    g_uart_num = uart_num;
    uart_param_config(g_uart_num, &uart_config);
    uart_set_pin(g_uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(g_uart_num, RX_BUF_SIZE, TX_BUF_SIZE, UART_QUEUE_LENGTH, &uart_queue, ESP_INTR_FLAG_IRAM);
    uart_set_rx_timeout(g_uart_num, 50);
    if (boudrate > 460800) {
        uart_set_rx_full_threshold(g_uart_num, 50);
    }

    xTaskCreate(uart_frame_task, "uart_queue_task", 3 * 1024, NULL, 10, &uart_frame_task_handle);
}

void yoke_rad60_protocol_deinit(void) {
    if (uart_frame_task_handle != NULL) {
        vTaskDelete(uart_frame_task_handle);
        uart_frame_task_handle = NULL;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (uart_buffer_queue != NULL) {
        uart_frame_t frame;
        while (xQueueReceive(uart_buffer_queue, &frame, 0) == pdTRUE) {
            yoke_rad60_protocol_free_frame_buffer(&frame);
        }
        vQueueDelete(uart_buffer_queue);
        uart_buffer_queue = NULL;
    }

    if (uart_semaphore != NULL) {
        vSemaphoreDelete(uart_semaphore);
        uart_semaphore = NULL;
    }

    if (g_uart_num != 0xFF) {
        (void)uart_driver_delete(g_uart_num);
        uart_queue = NULL;
        g_uart_num = 0xFF;
    }
}

void yoke_rad60_protocol_write_bytes(uint16_t cmd, const uint8_t* frame, uint32_t len) {
    if (uart_semaphore == NULL || g_uart_num == 0xFF) {
        return;
    }
    if (xSemaphoreTake(uart_semaphore, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    uint32_t total_len = 1 + 1 + 1 + len + 2;
    uint8_t* out_buf = (uint8_t*)malloc(total_len);
    if (out_buf == NULL) {
        xSemaphoreGive(uart_semaphore);
        return;
    }

    out_buf[0] = PACK_SEND_HEAD;
    out_buf[1] = cmd & 0xFF;
    out_buf[2] = len & 0xFF;
    if (len > 0 && frame != NULL) {
        memcpy(&out_buf[3], frame, len);
    }

    uint16_t check_sum = 0;
    for (uint32_t i = 0; i < 3 + len; i++) {
        check_sum += out_buf[i];
    }
    out_buf[3 + len] = check_sum & 0xFF;
    out_buf[3 + len + 1] = (check_sum >> 8) & 0xFF;

    (void)uart_write_bytes(g_uart_num, out_buf, total_len);
    free(out_buf);
    xSemaphoreGive(uart_semaphore);
}

int yoke_rad60_protocol_recv_frame(uart_frame_t* frame, uint32_t timeout_ms) {
    if (frame == NULL || uart_buffer_queue == NULL) {
        return 0;
    }
    if (timeout_ms != portMAX_DELAY) {
        timeout_ms = pdMS_TO_TICKS(timeout_ms);
    }
    return xQueueReceive(uart_buffer_queue, frame, timeout_ms);
}

void yoke_rad60_protocol_free_frame_buffer(uart_frame_t* frame) {
    if (frame == NULL) {
        return;
    }
    if (frame->frame_type == FRAME_TYPE_RESPONSE) {
        free(frame->response.buffer);
        frame->response.buffer = NULL;
    } else if (frame->frame_type == FRAME_TYPE_REPORT) {
        free(frame->report.buffer);
        frame->report.buffer = NULL;
    }
}

static void frame_recv_report_callback(uint8_t frame_head, uint8_t len, uint8_t type, const uint8_t* buf) {
    uart_frame_t frame;
    frame.frame_type = FRAME_TYPE_REPORT;
    frame.report.head = frame_head;
    frame.report.len = len;
    frame.report.type = type;
    uint8_t data_len = (len > 1) ? (len - 1) : 0;
    if (data_len > 0) {
        frame.report.buffer = (uint8_t*)malloc(data_len);
        if (frame.report.buffer == NULL) {
            return;
        }
        memcpy(frame.report.buffer, buf, data_len);
    } else {
        frame.report.buffer = NULL;
    }
    if (xQueueSend(uart_buffer_queue, &frame, pdMS_TO_TICKS(10)) != pdTRUE) {
        free(frame.report.buffer);
    }
}

static int32_t deal_uart_buffer(uint8_t* buffer, uint32_t data_length) {
    uint8_t* deal_ptr = buffer;
    uint32_t total_processed = 0;
    while (data_length > 0) {
        while (data_length > 0 && deal_ptr[0] != PACK_REPORT_HEAD) {
            deal_ptr++;
            data_length--;
            total_processed++;
        }
        if (data_length < YOKE_RAD60_FRAME_MIN_LEN) {
            break;
        }
        uint8_t len = deal_ptr[1];
        uint32_t packet_len = 1 + 1 + len + 1;
        if (len < 1 || packet_len > RX_FRAME_MAX_SIZE || data_length < packet_len) {
            break;
        }
        uint8_t check_sum = 0;
        for (uint32_t i = 0; i < 2 + len; i++) {
            check_sum += deal_ptr[i];
        }
        if (check_sum != deal_ptr[2 + len]) {
            deal_ptr++;
            data_length--;
            total_processed++;
            continue;
        }
        frame_recv_report_callback(deal_ptr[0], len, deal_ptr[2], &deal_ptr[3]);
        deal_ptr += packet_len;
        data_length -= packet_len;
        total_processed += packet_len;
    }
    return (int32_t)total_processed;
}

static void uart_frame_task(void* arg) {
    uart_event_t xEvent;
    static uint8_t rx_buf[RX_FRAME_MAX_SIZE + 5];
    uint32_t buffer_total_len = 0;
    while (1) {
        if (xQueueReceive(uart_queue, (void*)&xEvent, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (xEvent.type != UART_DATA) {
            continue;
        }

        size_t event_read_size = 0;
        uart_get_buffered_data_len(g_uart_num, &event_read_size);
        if (event_read_size == 0) {
            continue;
        }

        if (buffer_total_len + event_read_size > RX_FRAME_MAX_SIZE) {
            buffer_total_len = 0;
        }

        uart_read_bytes(g_uart_num, rx_buf + buffer_total_len, event_read_size, portMAX_DELAY);
        buffer_total_len += event_read_size;

        uint32_t used_size = (uint32_t)deal_uart_buffer(rx_buf, buffer_total_len);
        if (used_size > 0 && used_size <= buffer_total_len) {
            buffer_total_len -= used_size;
            if (buffer_total_len > 0) {
                memmove(rx_buf, rx_buf + used_size, buffer_total_len);
            }
        }
    }
}
