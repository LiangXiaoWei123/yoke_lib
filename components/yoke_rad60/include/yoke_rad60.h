#ifndef __YOKE_RAD60_RADAR_H__
#define __YOKE_RAD60_RADAR_H__

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "rootmaker_utils.h"

/* 仅保留测距/检测结果的主数据结构 */
typedef struct {
    uint16_t range_val; /* 距离值,单位: cm */
    int16_t angle_val;  /* 角度值,单位: 1° */
    int16_t velo_val;   /* 速度值,单位: cm/s */
    uint8_t objid;      /* 目标ID */
    uint8_t reserved0;  /* 暂无含义 */
} multitarget_obj_info_t;

typedef struct {
    uint8_t obj_num;               /* 目标数量 */
    uint8_t reserved[3];           /* 暂无含义 */
    multitarget_obj_info_t obj[2]; /* 目标信息，当前最多支持2个目标 */
} multitarget_det_info_t;

typedef struct {
    bool is_comm_ok;                   /* 通信是否正常 */
    bool is_detected;                  /* 是否检测到目标 */
    multitarget_det_info_t radar_info;  /* 雷达数据 */
} yoke_rad60_radar_data_t;

esp_err_t yoke_rad60_init(rootmaker_port_t port);
esp_err_t yoke_rad60_deinit(void);
esp_err_t yoke_rad60_get_radar_data(yoke_rad60_radar_data_t* data);

#endif
