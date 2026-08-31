/** @file yoke_rad60.c */
#include "yoke_rad60.h"

#include <string.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "yoke_rad60_protocol.h"

static const uint8_t RAD60_CMD_GET_RADAR_STATE = 0xD0;
static const uint8_t RAD60_REPORT_MULTI_TARGET = 0x0A;

static SemaphoreHandle_t radar_mutex = NULL;
static TaskHandle_t yoke_rad60_task_handle = NULL;
static uint32_t radar_init_time_ms = 0;
static rootmaker_port_t s_rad60_port;
static yoke_rad60_radar_data_t yoke_rad60_radar_data = {
    .is_comm_ok = true,
};

static void yoke_rad60_task(void* arg);

static void yoke_rad60_process_multitarget_det_info(uint8_t len, uint8_t* buffer) {
    (void)len;
    if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    yoke_rad60_radar_data.radar_info.obj_num = buffer[0];
    if (yoke_rad60_radar_data.radar_info.obj_num > 2) {
        yoke_rad60_radar_data.radar_info.obj_num = 2;
    }

    for (uint8_t i = 0; i < yoke_rad60_radar_data.radar_info.obj_num; i++) {
        uint8_t data_index = 4 + (i * 4);
        yoke_rad60_radar_data.radar_info.obj[i].range_val = (buffer[data_index] < 32) ? (buffer[data_index] * 6.25) : (buffer[data_index] * 6.85);
        yoke_rad60_radar_data.radar_info.obj[i].angle_val = (int8_t)buffer[data_index + 1];
        yoke_rad60_radar_data.radar_info.obj[i].velo_val = (int8_t)(buffer[data_index + 2] / 32);
        yoke_rad60_radar_data.radar_info.obj[i].objid = buffer[data_index + 3];
    }
    yoke_rad60_radar_data.is_detected = (yoke_rad60_radar_data.radar_info.obj_num > 0);
    xSemaphoreGive(radar_mutex);
}

static esp_err_t yoke_rad60_process_report_frame(uint8_t len, uint8_t type, uint8_t* buffer) {
    if (type != RAD60_REPORT_MULTI_TARGET) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    yoke_rad60_process_multitarget_det_info(len - 1, buffer);
    return ESP_OK;
}

static void yoke_rad60_task(void* arg) {
    (void)arg;
    uart_frame_t uart_frame;
    uint32_t last_data_time_ms = xTaskGetTickCount();
    uint32_t last_query_time_ms = 0;
    bool query_sent = false;
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        bool data_received = false;
        while (yoke_rad60_protocol_recv_frame(&uart_frame, 50)) {
            data_received = true;
            if (uart_frame.frame_type == FRAME_TYPE_REPORT) {
                if (yoke_rad60_process_report_frame(uart_frame.report.len, uart_frame.report.type, uart_frame.report.buffer) == ESP_OK) {
                    if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        yoke_rad60_radar_data.is_comm_ok = true;
                        xSemaphoreGive(radar_mutex);
                    }
                }
            }
            yoke_rad60_protocol_free_frame_buffer(&uart_frame);
        }

        if (data_received) {
            last_data_time_ms = xTaskGetTickCount();
            query_sent = false;
            if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                yoke_rad60_radar_data.is_comm_ok = true;
                xSemaphoreGive(radar_mutex);
            }
        }

        uint32_t current_time_ms = xTaskGetTickCount();
        if (current_time_ms - last_data_time_ms > 500) {
            if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                yoke_rad60_radar_data.is_detected = 0;
                yoke_rad60_radar_data.radar_info.obj_num = 0;
                xSemaphoreGive(radar_mutex);
            }

            if (!query_sent && current_time_ms - last_query_time_ms > 600) {
                yoke_rad60_protocol_write_bytes(RAD60_CMD_GET_RADAR_STATE, NULL, 0);
                last_query_time_ms = current_time_ms;
                query_sent = true;
            }

            if (query_sent && current_time_ms - last_query_time_ms > 1500 && current_time_ms - radar_init_time_ms > 5000) {
                if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    yoke_rad60_radar_data.is_comm_ok = false;
                    xSemaphoreGive(radar_mutex);
                }
                query_sent = false;
            }

            last_data_time_ms = current_time_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t yoke_rad60_init(rootmaker_port_t port) {
    gpio_num_t tx_pin;
    gpio_num_t rx_pin;
    esp_err_t pin_ret = rootmaker_get_port_pins(&port, &tx_pin, &rx_pin);
    if (pin_ret != ESP_OK) {
        return pin_ret;
    }

    if (radar_mutex != NULL || yoke_rad60_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t use_ret = rootmaker_use_port(&port);
    if (use_ret != ESP_OK) {
        return use_ret;
    }
    s_rad60_port = port;

    radar_mutex = xSemaphoreCreateMutex();
    if (radar_mutex == NULL) {
        (void)rootmaker_release_port(&s_rad60_port);
        return ESP_ERR_NO_MEM;
    }

    yoke_rad60_radar_data.is_comm_ok = true;
    yoke_rad60_radar_data.is_detected = 0;
    yoke_rad60_radar_data.radar_info.obj_num = 0;
    radar_init_time_ms = xTaskGetTickCount();

    yoke_rad60_protocol_init(UART_NUM, (int16_t)tx_pin, (int16_t)rx_pin, 921600);
    if (xTaskCreate(yoke_rad60_task, "yoke_rad60_task", 3 * 1024, NULL, 9, &yoke_rad60_task_handle) != pdPASS) {
        yoke_rad60_protocol_deinit();
        vSemaphoreDelete(radar_mutex);
        radar_mutex = NULL;
        (void)rootmaker_release_port(&s_rad60_port);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t yoke_rad60_deinit(void) {
    if (yoke_rad60_task_handle != NULL) {
        vTaskDelete(yoke_rad60_task_handle);
        yoke_rad60_task_handle = NULL;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    yoke_rad60_protocol_deinit();

    if (radar_mutex != NULL) {
        vSemaphoreDelete(radar_mutex);
        radar_mutex = NULL;
    }

    (void)rootmaker_release_port(&s_rad60_port);
    return ESP_OK;
}

esp_err_t yoke_rad60_get_radar_data(yoke_rad60_radar_data_t* data) {
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (radar_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(radar_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memcpy(data, &yoke_rad60_radar_data, sizeof(*data));
    xSemaphoreGive(radar_mutex);
    return yoke_rad60_radar_data.is_comm_ok ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}
