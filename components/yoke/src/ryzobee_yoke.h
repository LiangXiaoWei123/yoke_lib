#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

typedef enum {
    YOKE_PORT_RS,
    YOKE_PORT_S_BUS,
    YOKE_PORT_F_BUS,
} yoke_port_type_t;

typedef enum {
    YOKE_BUS_NONE,
    YOKE_BUS_I2C,
    YOKE_BUS_UART,
    YOKE_BUS_GPIO,
    YOKE_BUS_SPI,
} yoke_bus_type_t;

#define YOKE_PORT_MAX_PINS 16

typedef struct {
    yoke_port_type_t type;
    yoke_bus_type_t bus_type;
    gpio_num_t pins[YOKE_PORT_MAX_PINS];
    uint8_t pin_count;
    bool claimed;
} yoke_port_t;

esp_err_t yoke_port_claim(yoke_port_t *port, yoke_port_type_t type,
                          yoke_bus_type_t bus_type, uint8_t min_pins);
void yoke_port_release(yoke_port_t *port);
