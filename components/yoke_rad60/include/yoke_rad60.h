#ifndef __YOKE_RAD60_RADAR_H__
#define __YOKE_RAD60_RADAR_H__

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "rootmaker_utils.h"

/* Primary distance and detection result types. */
typedef struct {
    uint16_t range_val; /* Distance in cm. */
    int16_t angle_val;  /* Angle in degrees. */
    int16_t velo_val;   /* Velocity in cm/s. */
    uint8_t objid;      /* Target ID. */
    uint8_t reserved0;  /* Reserved. */
} multitarget_obj_info_t;

typedef struct {
    uint8_t obj_num;               /* Number of detected targets. */
    uint8_t reserved[3];           /* Reserved. */
    multitarget_obj_info_t obj[2]; /* Target data; up to two targets are supported. */
} multitarget_det_info_t;

typedef struct {
    bool is_comm_ok;                   /* True when radar communication is healthy. */
    bool is_detected;                  /* True when at least one target is detected. */
    multitarget_det_info_t radar_info; /* Radar detection data. */
} yoke_rad60_radar_data_t;

esp_err_t yoke_rad60_init(rootmaker_port_t port);
esp_err_t yoke_rad60_deinit(void);
esp_err_t yoke_rad60_get_radar_data(yoke_rad60_radar_data_t* data);

#endif
