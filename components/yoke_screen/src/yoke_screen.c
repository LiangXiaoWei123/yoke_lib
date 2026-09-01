/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "rootmaker_utils.h"
#include "rootmaker_pin.h"
#include "lcd_panel_st7789_spec.h"

#define YOKE_SCREEN_LVGL_TASK_STACK_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

#if CONFIG_SPIRAM && SOC_PSRAM_DMA_CAPABLE
#define YOKE_SCREEN_LVGL_DRAW_BUFFER_IN_EXTERNAL_MEMORY 1
#else
#define YOKE_SCREEN_LVGL_DRAW_BUFFER_IN_EXTERNAL_MEMORY 0
#endif

/* LCD size */
#define LCD_H_RES (240)
#define LCD_V_RES (240)

/* LCD settings */
#define LCD_SPI_NUM (SPI3_HOST)
#define LCD_PIXEL_CLK_HZ (80 * 1000 * 1000)
#define LCD_CMD_BITS (8)
#define LCD_PARAM_BITS (8)
#define LCD_BITS_PER_PIXEL (16)
#define LCD_DRAW_BUFF_DOUBLE (1)
#define LCD_BL_ON_LEVEL (1)

/* CST816T touch settings */
#define TOUCH_I2C_PORT I2C_NUM_1
#define TOUCH_I2C_CLOCK_HZ 400000
#define TOUCH_I2C_ADDRESS 0x15
#define TOUCH_I2C_TIMEOUT_MS 20

/* LCD pins */
#define LCD_GPIO_SCLK (LCD_SCK)
#define LCD_GPIO_MOSI (LCD_SDA)
#define LCD_GPIO_RST (LCD_RST)
#define LCD_GPIO_DC (LCD_RS)
#define LCD_GPIO_CS (LCD_CS)
#define LCD_GPIO_BL (LCD_BL)

static const char* TAG = "SCREEN";

static const st7789_lcd_init_cmd_t st7789_boe154_init[] = {
    // { cmd, data, data_size, delay_ms }

    { 0x11, NULL,                                                                                              0,  120 }, // Sleep Out

    { 0x36, (uint8_t[]){ 0x00 },                                                                               1,  0   }, // MADCTL
    { 0x3A, (uint8_t[]){ 0x05 },                                                                               1,  0   }, // COLMOD: 16bit
    { 0x21, NULL,                                                                                              0,  0   }, // Display inversion ON

    // CASET
    { 0x2A, (uint8_t[]){ 0x00, 0x00, 0x00, 0xEF },                                                             4,  0   },
    // RASET
    { 0x2B, (uint8_t[]){ 0x00, 0x00, 0x00, 0xEF },                                                             4,  0   },

    // Porch
    { 0xB2, (uint8_t[]){ 0x0C, 0x0C, 0x00, 0x33, 0x33 },                                                       5,  0   },
    // Gate control
    { 0xB7, (uint8_t[]){ 0x35 },                                                                               1,  0   },

    // Power & VCOM
    { 0xBB, (uint8_t[]){ 0x1F },                                                                               1,  0   },
    { 0xC0, (uint8_t[]){ 0x2C },                                                                               1,  0   },
    { 0xC2, (uint8_t[]){ 0x01 },                                                                               1,  0   },
    { 0xC3, (uint8_t[]){ 0x12 },                                                                               1,  0   },
    { 0xC4, (uint8_t[]){ 0x20 },                                                                               1,  0   },
    { 0xC6, (uint8_t[]){ 0x0F },                                                                               1,  0   },
    { 0xD0, (uint8_t[]){ 0xA4, 0xA1 },                                                                         2,  0   },

    // GAMMA 正极
    { 0xE0, (uint8_t[]){ 0xD0, 0x08, 0x11, 0x08, 0x0C, 0x15, 0x39, 0x33, 0x50, 0x36, 0x13, 0x14, 0x29, 0x2D }, 14, 0   },

    // GAMMA 负极
    { 0xE1, (uint8_t[]){ 0xD0, 0x08, 0x10, 0x08, 0x06, 0x06, 0x39, 0x44, 0x51, 0x0B, 0x16, 0x14, 0x2F, 0x31 }, 14, 0   },

    { 0x29, NULL,                                                                                              0,  0   }  // Display ON
};

static st7789_vendor_config_t st7789_boe154_vendor_config = {
    .init_cmds = st7789_boe154_init,
    .init_cmds_size = sizeof(st7789_boe154_init) / sizeof(st7789_boe154_init[0]),
};

/* LCD IO and panel */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static bool lcd_spi_initialized = false;

/* LVGL display and touch */
static lv_display_t* lvgl_disp = NULL;
static lv_indev_t* touch_indev = NULL;
static i2c_master_bus_handle_t touch_i2c_bus = NULL;
static i2c_master_dev_handle_t touch_i2c_device = NULL;
static rootmaker_port_t touch_i2c_port;
static bool touch_i2c_bus_ref_held = false;

static esp_err_t touch_init(void) {
    if (touch_i2c_device != NULL)
        return ESP_ERR_INVALID_STATE;

    const gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << TP_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&reset_config), TAG, "Touch reset GPIO configuration failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(TP_RST, 0), TAG, "Touch reset assert failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(TP_RST, 1), TAG, "Touch reset release failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    rootmaker_port_t sys_port = rootmaker_sys_i2c_port;
    esp_err_t ret = rootmaker_new_i2c_bus(&sys_port, &touch_i2c_bus);
    ESP_RETURN_ON_ERROR(ret, TAG, "Touch I2C bus initialization failed");
    touch_i2c_port = sys_port;
    touch_i2c_bus_ref_held = true;

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_I2C_ADDRESS,
        .scl_speed_hz = TOUCH_I2C_CLOCK_HZ,
    };
    ret = i2c_master_bus_add_device(touch_i2c_bus, &device_config, &touch_i2c_device);
    if (ret != ESP_OK) {
        (void)rootmaker_del_i2c_bus(&sys_port);
        touch_i2c_bus = NULL;
        touch_i2c_bus_ref_held = false;
    }
    return ret;
}

static bool touch_read(uint16_t* x, uint16_t* y) {
    if (touch_i2c_device == NULL || x == NULL || y == NULL)
        return false;

    uint8_t data[6] = { 0 };
    const uint8_t register_address = 0x01;
    if (i2c_master_transmit_receive(touch_i2c_device, &register_address, sizeof(register_address), data, sizeof(data), TOUCH_I2C_TIMEOUT_MS) != ESP_OK ||
        data[1] == 0) {
        return false;
    }

    uint16_t raw_x = ((uint16_t)(data[2] & 0x0f) << 8) | data[3];
    uint16_t raw_y = ((uint16_t)(data[4] & 0x0f) << 8) | data[5];
    if (raw_x >= LCD_H_RES || raw_y >= LCD_V_RES)
        return false;

    /* Board orientation: mirror both axes, matching the previous touch configuration. */
    *x = LCD_H_RES - raw_x - 1;
    *y = LCD_V_RES - raw_y - 1;
    return true;
}

static esp_err_t app_lcd_init(void) {
    esp_err_t ret = ESP_OK;

    /* LCD backlight */
    gpio_config_t bk_gpio_config = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << LCD_GPIO_BL };
    ESP_GOTO_ON_ERROR(gpio_config(&bk_gpio_config), err, TAG, "Backlight GPIO configuration failed");

    /* LCD initialization */
    ESP_LOGD(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_GPIO_SCLK,
        .mosi_io_num = LCD_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 64 * 1024,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO), TAG, "SPI init failed");
    lcd_spi_initialized = true;

    ESP_LOGD(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_GPIO_DC,
        .cs_gpio_num = LCD_GPIO_CS,
        .pclk_hz = LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 3,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_NUM, &io_config, &lcd_io), err, TAG, "New panel IO failed");

    ESP_LOGD(TAG, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_GPIO_RST,
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#endif
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
        .vendor_config = &st7789_boe154_vendor_config,
    };
    ESP_GOTO_ON_ERROR(lcd_new_panel_st7789_spec(lcd_io, &panel_config, &lcd_panel), err, TAG, "New panel failed");

    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(lcd_panel), err, TAG, "Panel reset failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(lcd_panel), err, TAG, "Panel initialization failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_set_gap(lcd_panel, 0, 320 - 240), err, TAG, "Panel gap setup failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(lcd_panel, true), err, TAG, "Panel enable failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_swap_xy(lcd_panel, true), err, TAG, "Panel rotation setup failed");

    /* LCD backlight on */
    ESP_GOTO_ON_ERROR(gpio_set_level(LCD_GPIO_BL, LCD_BL_ON_LEVEL), err, TAG, "Backlight enable failed");

    return ret;

err:
    if (lcd_panel) {
        esp_lcd_panel_del(lcd_panel);
    }
    if (lcd_io) {
        esp_lcd_panel_io_del(lcd_io);
    }
    if (lcd_spi_initialized) {
        spi_bus_free(LCD_SPI_NUM);
        lcd_spi_initialized = false;
    }
    lcd_panel = NULL;
    lcd_io = NULL;
    return ret;
}

static void lvgl_port_touchpad_read(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    uint16_t x;
    uint16_t y;
    if (touch_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

static esp_err_t lvgl_tp_init(lv_display_t* disp) {
    ESP_RETURN_ON_ERROR(touch_init(), TAG, "Touch initialization failed");

    lvgl_port_lock(0);
    /* Register a touchpad input device */
    touch_indev = lv_indev_create();
    if (touch_indev == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_NO_MEM;
    }
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, lvgl_port_touchpad_read);
    lv_indev_set_disp(touch_indev, disp);
    lvgl_port_unlock();
    return ESP_OK;
}

static esp_err_t app_lvgl_init(void) {
    /* Initialize LVGL */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,       /* LVGL task priority */
        .task_stack = 10240,      /* LVGL task stack size */
        .task_affinity = -1,      /* LVGL task pinned to core (-1 is no affinity) */
        .task_max_sleep_ms = 500, /* Maximum sleep in LVGL task */
        .task_stack_caps = YOKE_SCREEN_LVGL_TASK_STACK_CAPS,
        .timer_period_ms = 5 /* LVGL timer tick period in ms */
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port initialization failed");

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size = LCD_H_RES * LCD_V_RES * 2,
        .double_buffer = LCD_DRAW_BUFF_DOUBLE,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation = {
            .swap_xy = false,
            .mirror_x = true,
            .mirror_y = true,
        },
        .flags = {
            .buff_spiram = YOKE_SCREEN_LVGL_DRAW_BUFFER_IN_EXTERNAL_MEMORY,
            .buff_dma = true,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = true,
#endif
        }
    };

    lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "LVGL display buffer allocation failed");
        lvgl_port_deinit();
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = lvgl_tp_init(lvgl_disp);
    if (ret != ESP_OK) {
        lvgl_port_remove_disp(lvgl_disp);
        lvgl_disp = NULL;
        lvgl_port_deinit();
        return ret;
    }

    return ESP_OK;
}

static esp_err_t screen_cleanup(void) {
    esp_err_t first_error = ESP_OK;

    gpio_set_level(LCD_GPIO_BL, 0);

    if (touch_indev != NULL) {
        lvgl_port_lock(0);
        lv_indev_delete(touch_indev);
        lvgl_port_unlock();
        touch_indev = NULL;
    }
    if (lvgl_disp != NULL) {
        esp_err_t ret = lvgl_port_remove_disp(lvgl_disp);
        if (ret != ESP_OK && first_error == ESP_OK)
            first_error = ret;
        if (ret == ESP_OK)
            lvgl_disp = NULL;
    }
    if (touch_i2c_device != NULL) {
        esp_err_t ret = i2c_master_bus_rm_device(touch_i2c_device);
        if (ret != ESP_OK && first_error == ESP_OK)
            first_error = ret;
        if (ret == ESP_OK)
            touch_i2c_device = NULL;
    }
    if (lvgl_disp != NULL || touch_i2c_device != NULL)
        return first_error;

    esp_err_t ret = lvgl_port_deinit();
    if (ret != ESP_OK && first_error == ESP_OK)
        first_error = ret;

    if (touch_i2c_bus_ref_held) {
        ret = rootmaker_del_i2c_bus(&touch_i2c_port);
        if (ret != ESP_OK && first_error == ESP_OK)
            first_error = ret;
        if (ret == ESP_OK) {
            touch_i2c_bus_ref_held = false;
            touch_i2c_bus = NULL;
        }
    }

    if (lcd_panel != NULL) {
        ret = esp_lcd_panel_disp_on_off(lcd_panel, false);
        if (ret != ESP_OK && first_error == ESP_OK)
            first_error = ret;
        ret = esp_lcd_panel_del(lcd_panel);
        if (ret != ESP_OK && first_error == ESP_OK)
            first_error = ret;
        if (ret == ESP_OK)
            lcd_panel = NULL;
    }
    if (lcd_io != NULL) {
        ret = esp_lcd_panel_io_del(lcd_io);
        if (ret != ESP_OK && first_error == ESP_OK)
            first_error = ret;
        if (ret == ESP_OK)
            lcd_io = NULL;
    }
    if (lcd_spi_initialized) {
        ret = spi_bus_free(LCD_SPI_NUM);
        if (ret != ESP_OK && first_error == ESP_OK)
            first_error = ret;
        if (ret == ESP_OK)
            lcd_spi_initialized = false;
    }
    gpio_reset_pin(TP_RST);
    return first_error;
}

lv_display_t* yoke_screen_with_lvgl_init(void) {
    if (lcd_panel != NULL || lvgl_disp != NULL || touch_i2c_device != NULL)
        return NULL;

    /* LCD HW initialization */
    if (app_lcd_init() != ESP_OK) {
        return NULL;
    }

    /* LVGL initialization */
    if (app_lvgl_init() != ESP_OK) {
        (void)screen_cleanup();
        return NULL;
    }

    return lvgl_disp;
}

esp_err_t yoke_screen_deinit(void) {
    if (lcd_panel == NULL && lcd_io == NULL && !lcd_spi_initialized &&
        lvgl_disp == NULL && touch_indev == NULL && touch_i2c_device == NULL &&
        !touch_i2c_bus_ref_held) {
        return ESP_ERR_INVALID_STATE;
    }
    return screen_cleanup();
}

void yoke_screen_draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* data) {
    if (lcd_panel == NULL || data == NULL || w == 0 || h == 0 || x >= LCD_H_RES || y >= LCD_V_RES ||
        w > LCD_H_RES - x || h > LCD_V_RES - y) {
        ESP_LOGE(TAG, "Invalid bitmap: x=%u y=%u w=%u h=%u", x, y, w, h);
        return;
    }

    esp_lcd_panel_draw_bitmap(lcd_panel, x, y, x + w, y + h, data);
}
