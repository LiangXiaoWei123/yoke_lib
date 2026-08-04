#include "ryzobee_yoke.h"

esp_err_t yoke_port_claim(yoke_port_t *port, yoke_port_type_t type,
                          yoke_bus_type_t bus_type, uint8_t min_pins)
{
    if (port == NULL || port->claimed || port->type != type ||
        port->bus_type != bus_type || port->pin_count < min_pins) {
        return ESP_ERR_INVALID_ARG;
    }
    port->claimed = true;
    return ESP_OK;
}

void yoke_port_release(yoke_port_t *port)
{
    if (port != NULL) port->claimed = false;
}
