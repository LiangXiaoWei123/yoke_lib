#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "yoke.h"

const char *TAG = "main";

#define RADAR_UART_NUM  UART_NUM_1
#define RADAR_BAUDRATE  921600U

#define EC11_I2C_PORT   I2C_NUM_0

typedef enum {
    UI_IF_PORT_P1 = 0,
    UI_IF_PORT_P2,
    UI_IF_PORT_P3,
    UI_IF_PORT_P4,
    UI_IF_PORT_MAX,
} ui_if_port_t;

typedef struct {
    const char *name;
    gpio_num_t pin_a;
    gpio_num_t pin_b;
} ui_if_port_config_t;

/*
 * Interface map used by global UI port selector:
 * - I2C  : pin_a=SDA, pin_b=SCL
 * - UART : pin_a=TX,  pin_b=RX
 * - Motor: pin_a=PWMA, pin_b=PWMB
 */
static const ui_if_port_config_t s_if_port_configs[UI_IF_PORT_MAX] = {
    /* Connector order is GND, 3V3, SCL, SDA. pin_a=SDA, pin_b=SCL. */
    { .name = "P1", .pin_a = GPIO_NUM_15, .pin_b = GPIO_NUM_16 },
    { .name = "P2", .pin_a = GPIO_NUM_19, .pin_b = GPIO_NUM_20 },
    { .name = "P3", .pin_a = GPIO_NUM_13, .pin_b = GPIO_NUM_14 },
    { .name = "P4", .pin_a = GPIO_NUM_17, .pin_b = GPIO_NUM_18 },
};

static ui_if_port_t s_ec11_port = UI_IF_PORT_P1;
static ui_if_port_t s_radar_port = UI_IF_PORT_P2;
static ui_if_port_t s_motor_port = UI_IF_PORT_P4;
static ui_if_port_t s_rgbw_port = UI_IF_PORT_P1;
static ui_if_port_t s_keyw_port = UI_IF_PORT_P4;

// A selected interface is reserved by its active module until it is deinitialized.

static bool if_port_is_occupied(ui_if_port_t port, const char **reason);

static bool s_radar_initialized;
static bool s_ec11_initialized;
static bool s_rgbw_initialized;
static i2c_master_bus_handle_t s_ec11_i2c_bus;
static yoke_ec11_t s_ec11;
static yoke_moto_t s_motor;
static yoke_keyw_t s_keyw;
static yoke_zxacc_maker_t s_zxacc_maker;

static const ui_if_port_config_t *if_port_get_config(ui_if_port_t port)
{
    if ((unsigned)port >= UI_IF_PORT_MAX) return &s_if_port_configs[0];
    return &s_if_port_configs[port];
}

/* ============================ Touch UI ============================ */

#define UI_COLOR_BG        lv_color_hex(0x0B1220)
#define UI_COLOR_SURFACE   lv_color_hex(0x111C2E)
#define UI_COLOR_CARD      lv_color_hex(0x17243A)
#define UI_COLOR_CARD_ON   lv_color_hex(0x243754)
#define UI_COLOR_ACCENT    lv_color_hex(0x38BDF8)
#define UI_COLOR_TEXT      lv_color_hex(0xF8FAFC)
#define UI_COLOR_SUBTEXT   lv_color_hex(0x94A3B8)
#define UI_COLOR_DANGER    lv_color_hex(0xEF4444)
#define UI_COLOR_SUCCESS   lv_color_hex(0x22C55E)
#define UI_COLOR_WARNING   lv_color_hex(0xF59E0B)

typedef enum {
    UI_PAGE_DASHBOARD = 0,
    UI_PAGE_EC11,
    UI_PAGE_RGBW,
    UI_PAGE_RADAR,
    UI_PAGE_MOTOR,
    UI_PAGE_ZXACC,
    UI_PAGE_KEYW,
    UI_PAGE_MAX
} ui_page_t;

#define KEYW_LOG_MAX 8U

static lv_obj_t *s_pages[UI_PAGE_MAX];
static ui_page_t s_current_page = UI_PAGE_DASHBOARD;
static lv_obj_t *s_status_label;
static lv_obj_t *s_port_selector_box;
static lv_obj_t *s_port_selector_buttons[UI_IF_PORT_MAX];
static ui_if_port_t s_global_port = UI_IF_PORT_P1;

/* Dashboard status dots, index matches ui_page_t order minus DASHBOARD */
static lv_obj_t *s_dash_dots[UI_PAGE_MAX - 1];
static lv_obj_t *s_dash_state_labels[UI_PAGE_MAX - 1];

/* EC11 page widgets */
static lv_obj_t *s_ec11_count_label;
static lv_obj_t *s_ec11_key_label;

/* Radar page widgets */
static lv_obj_t *s_radar_comm_dot;
static lv_obj_t *s_radar_comm_label;
static lv_obj_t *s_radar_detect_dot;
static lv_obj_t *s_radar_detect_label;
static lv_obj_t *s_radar_obj_labels[2];
static lv_obj_t *s_radar_enable_switch;

/* ZXACC page widgets */
static lv_obj_t *s_zxacc_version_label;
static lv_obj_t *s_zxacc_voltage_label;
static lv_obj_t *s_zxacc_charge_label;
static lv_obj_t *s_zxacc_shutdown_label;
static uint32_t s_zxacc_shutdown_seconds = 0;

/* Motor page widgets */
static lv_obj_t *s_motor_state_label;

/* KEYW page widgets */
static lv_obj_t *s_keyw_log_labels[4];
static lv_obj_t *s_keyw_led_switch;
static bool s_keyw_led_on = true;

/* KEYW event ring buffer */
static button_event_t s_keyw_event_log[KEYW_LOG_MAX];
static uint8_t s_keyw_event_head;
static uint8_t s_keyw_event_count;
static portMUX_TYPE s_keyw_mux = portMUX_INITIALIZER_UNLOCKED;

static const char *keyw_event_to_string(button_event_t event)
{
    switch (event) {
    case BUTTON_PRESS_DOWN: return "press down";
    case BUTTON_PRESS_UP: return "press up";
    case BUTTON_PRESS_REPEAT: return "press repeat";
    case BUTTON_PRESS_REPEAT_DONE: return "press repeat done";
    case BUTTON_SINGLE_CLICK: return "single click";
    case BUTTON_DOUBLE_CLICK: return "double click";
    case BUTTON_MULTIPLE_CLICK: return "multiple click";
    case BUTTON_LONG_PRESS_START: return "long press start";
    case BUTTON_LONG_PRESS_HOLD: return "long press hold";
    case BUTTON_LONG_PRESS_UP: return "long press up";
    case BUTTON_PRESS_END: return "press end";
    default: return "unknown";
    }
}

static void rgbw_set_all(uint8_t red, uint8_t green, uint8_t blue, uint8_t white)
{
    if (!s_rgbw_initialized) {
        ESP_LOGW(TAG, "RGBW is not initialized; input L or tap Start first");
        return;
    }
    esp_err_t ret = yoke_rgbw_set_all(red, green, blue, white);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RGBW 设置失败: %s", esp_err_to_name(ret));
    }
}

static void rgbw_init(void)
{
    if (s_rgbw_initialized) {
        ESP_LOGW(TAG, "RGBW already initialized");
        return;
    }
    const char *owner = NULL;
    if (if_port_is_occupied(s_global_port, &owner)) {
        ESP_LOGE(TAG, "RGBW init blocked: %s is in use by %s",
                 if_port_get_config(s_global_port)->name, owner);
        return;
    }
    const ui_if_port_config_t *port = if_port_get_config(s_global_port);
    const yoke_rgbw_config_t config = {
        .gpio_num = port->pin_a,
        .led_num = 3,
    };
    esp_err_t ret = yoke_rgbw_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RGBW init failed: %s", esp_err_to_name(ret));
        return;
    }
    s_rgbw_initialized = true;
    s_rgbw_port = s_global_port;
    rgbw_set_all(0, 0, 0, 0);
    ESP_LOGI(TAG, "RGBW initialized on %s: data=GPIO%d, %d LEDs", port->name,
             config.gpio_num, config.led_num);
}

static void rgbw_deinit(void)
{
    if (!s_rgbw_initialized) {
        ESP_LOGW(TAG, "RGBW not initialized");
        return;
    }
    esp_err_t ret = yoke_rgbw_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RGBW deinit failed: %s", esp_err_to_name(ret));
        return;
    }
    s_rgbw_initialized = false;
    ESP_LOGI(TAG, "RGBW deinitialized");
}

static void keyw_event_callback(button_event_t event, void *user_data)
{
    (void)user_data;
    ESP_LOGI(TAG, "Yoke-KEYW event: %s", keyw_event_to_string(event));

    portENTER_CRITICAL(&s_keyw_mux);
    s_keyw_event_log[s_keyw_event_head] = event;
    s_keyw_event_head = (s_keyw_event_head + 1) % KEYW_LOG_MAX;
    if (s_keyw_event_count < KEYW_LOG_MAX) ++s_keyw_event_count;
    portEXIT_CRITICAL(&s_keyw_mux);
}

static void keyw_init(void)
{
    if (s_keyw.initialized) {
        ESP_LOGW(TAG, "KEYW already initialized");
        return;
    }
    const char *owner = NULL;
    if (if_port_is_occupied(s_global_port, &owner)) {
        ESP_LOGE(TAG, "KEYW init blocked: %s is in use by %s",
                 if_port_get_config(s_global_port)->name, owner);
        return;
    }
    const ui_if_port_config_t *port = if_port_get_config(s_global_port);
    yoke_keyw_config_t config = yoke_keyw_default_config();
    config.button_gpio_num = port->pin_a;
    config.led_gpio_num = port->pin_b;
    config.led_initial_on = s_keyw_led_on;
    esp_err_t ret = yoke_keyw_init(&s_keyw, &config);
    if (ret == ESP_OK) ret = yoke_keyw_register_event_callback(
        &s_keyw, BUTTON_PRESS_DOWN, keyw_event_callback, NULL);
    if (ret == ESP_OK) ret = yoke_keyw_register_event_callback(
        &s_keyw, BUTTON_PRESS_UP, keyw_event_callback, NULL);
    if (ret == ESP_OK) ret = yoke_keyw_register_event_callback(
        &s_keyw, BUTTON_SINGLE_CLICK, keyw_event_callback, NULL);
    if (ret == ESP_OK) ret = yoke_keyw_register_event_callback(
        &s_keyw, BUTTON_LONG_PRESS_START, keyw_event_callback, NULL);
    if (ret == ESP_OK) ret = yoke_keyw_register_event_callback(
        &s_keyw, BUTTON_LONG_PRESS_UP, keyw_event_callback, NULL);
    if (ret != ESP_OK && s_keyw.initialized) (void)yoke_keyw_deinit(&s_keyw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "KEYW init failed: %s", esp_err_to_name(ret));
        return;
    }
    s_keyw_port = s_global_port;
    ESP_LOGI(TAG, "KEYW initialized on %s: button=GPIO%d, LED=GPIO%d", port->name,
             config.button_gpio_num, config.led_gpio_num);
}

static void keyw_deinit(void)
{
    if (!s_keyw.initialized) {
        ESP_LOGW(TAG, "KEYW not initialized");
        return;
    }
    esp_err_t ret = yoke_keyw_deinit(&s_keyw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "KEYW deinit failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "KEYW deinitialized");
}

static bool if_port_equals(ui_if_port_t lhs, ui_if_port_t rhs)
{
    return lhs == rhs;
}

static bool if_port_conflicts_with_keyw(ui_if_port_t port)
{
    return s_keyw.initialized && if_port_equals(s_keyw_port, port);
}

static bool if_port_is_occupied(ui_if_port_t port, const char **reason)
{
    if (if_port_conflicts_with_keyw(port) && s_keyw.initialized) {
        if (reason) *reason = "KEYW";
        return true;
    }
    if (s_ec11_initialized && if_port_equals(s_ec11_port, port)) {
        if (reason) *reason = "EC11";
        return true;
    }
    if (s_radar_initialized && if_port_equals(s_radar_port, port)) {
        if (reason) *reason = "Radar";
        return true;
    }
    if (s_motor.initialized && if_port_equals(s_motor_port, port)) {
        if (reason) *reason = "Motor";
        return true;
    }
    if (s_rgbw_initialized && if_port_equals(s_rgbw_port, port)) {
        if (reason) *reason = "RGBW";
        return true;
    }
    if (reason) *reason = NULL;
    return false;
}

static void ui_refresh_port_selector(void)
{
    for (int i = 0; i < UI_IF_PORT_MAX; ++i) {
        lv_obj_t *btn = s_port_selector_buttons[i];
        if (btn == NULL) continue;

        ui_if_port_t port = (ui_if_port_t)i;
        const char *owner = NULL;
        bool occupied = if_port_is_occupied(port, &owner);
        bool selected = if_port_equals(port, s_global_port);

        if (occupied && !selected) {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x64748B), 0);
        } else {
            lv_obj_remove_state(btn, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(btn, selected ? UI_COLOR_ACCENT : UI_COLOR_CARD, 0);
        }

        if (occupied && selected) {
            lv_obj_set_style_bg_color(btn, UI_COLOR_WARNING, 0);
        }

        lv_obj_set_style_bg_color(btn, lv_color_darken(UI_COLOR_CARD, 15), LV_STATE_DISABLED);
    }

}

static bool zxacc_maker_is_ready(void)
{
    if (!s_zxacc_maker.initialized) {
        ESP_LOGW(TAG, "ZXACC-maker is not initialized; input p first");
        return false;
    }
    return true;
}

static void zxacc_maker_init(void)
{
    if (s_zxacc_maker.initialized) {
        ESP_LOGW(TAG, "ZXACC-maker is already initialized");
        return;
    }

    yoke_zxacc_maker_config_t config = yoke_zxacc_maker_default_config();
    /*
     * The power baseboard owns the GPIO41/40 system I2C bus.  It must work
     * without a display attached; the touch driver joins this bus later.
     */
    config.i2c_port = I2C_NUM_1;

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = config.i2c_port,
        .sda_io_num = config.sda_gpio_num,
        .scl_io_num = config.scl_gpio_num,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t system_i2c_bus = NULL;
    esp_err_t ret = i2c_new_master_bus(&bus_config, &system_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ZXACC system I2C1 initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    config.i2c_bus = system_i2c_bus;
    ret = yoke_zxacc_maker_init(&s_zxacc_maker, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ZXACC-maker I2C init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = i2c_master_probe(s_zxacc_maker.i2c_bus, config.i2c_address, 1000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ZXACC-maker found on system I2C1: SDA=%d SCL=%d address=0x%02X",
                 config.sda_gpio_num, config.scl_gpio_num, config.i2c_address);
    } else {
        ESP_LOGW(TAG, "ZXACC-maker initialized but 0x%02X did not respond: %s",
                 config.i2c_address, esp_err_to_name(ret));
    }
}

static void zxacc_maker_scan(void)
{
    if (!zxacc_maker_is_ready()) return;

    ESP_LOGI(TAG, "ZXACC-maker I2C scan:");
    uint8_t found = 0;
    for (uint8_t address = 0x08; address < 0x78; ++address) {
        if (i2c_master_probe(s_zxacc_maker.i2c_bus, address, 100) == ESP_OK) {
            ESP_LOGI(TAG, "  found I2C device at 0x%02X", address);
            ++found;
        }
    }
    ESP_LOGI(TAG, "I2C scan complete: %u device(s)", (unsigned)found);
}

static void zxacc_maker_print_status(void)
{
    if (!zxacc_maker_is_ready()) return;

    uint16_t firmware = 0;
    uint32_t voltage_mv = 0;
    yoke_zxacc_maker_charge_state_t charge = YOKE_ZXACC_MAKER_CHARGE_UNKNOWN;

    esp_err_t ret = yoke_zxacc_maker_get_firmware_version(&s_zxacc_maker, &firmware);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ZXACC-maker firmware read failed: %s", esp_err_to_name(ret));
        return;
    }
    yoke_zxacc_maker_get_battery_voltage(&s_zxacc_maker, &voltage_mv);
    yoke_zxacc_maker_get_charge_state(&s_zxacc_maker, &charge);

    const char *charge_str = "unknown";
    if (charge == YOKE_ZXACC_MAKER_CHARGE_CHARGING) charge_str = "charging";
    if (charge == YOKE_ZXACC_MAKER_CHARGE_NOT_CHARGING) charge_str = "not charging";
    if (charge == YOKE_ZXACC_MAKER_CHARGE_COMPLETE) charge_str = "complete";
    ESP_LOGI(TAG, "ZXACC-maker: firmware=0x%04X charge=%s (0x%02X) voltage=%lu mV (%.3f V)",
             firmware, charge_str, (unsigned)charge,
             (unsigned long)voltage_mv, voltage_mv / 1000.0f);
}

static void zxacc_maker_print_timings(void)
{
    if (!zxacc_maker_is_ready()) return;

    uint16_t wakeup_time_ms;
    uint16_t shutdown_time_ms;
    esp_err_t ret = yoke_zxacc_maker_get_wakeup_time(&s_zxacc_maker, &wakeup_time_ms);
    if (ret == ESP_OK) ret = yoke_zxacc_maker_get_shutdown_time(&s_zxacc_maker, &shutdown_time_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ZXACC-maker timing read failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "ZXACC-maker: wakeup hold=%u ms, shutdown hold=%u ms", (unsigned)wakeup_time_ms,
             (unsigned)shutdown_time_ms);
}

static void zxacc_maker_set_default_timings(void)
{
    if (!zxacc_maker_is_ready()) return;

    esp_err_t ret = yoke_zxacc_maker_set_wakeup_time(&s_zxacc_maker, 2000);
    if (ret == ESP_OK) ret = yoke_zxacc_maker_set_shutdown_time(&s_zxacc_maker, 2000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ZXACC-maker timing write failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "ZXACC-maker wakeup and shutdown hold times set to 2000 ms");
}

static void zxacc_maker_set_timer_wakeup_interactive(void)
{
    if (!zxacc_maker_is_ready()) return;

    ESP_LOGW(TAG, "ZXACC-maker timed wake-up: enter seconds (1-4294967295), then press Enter");
    ESP_LOGW(TAG, "After the value is written, the baseboard immediately enters low-power mode");

    char input[11] = {0};
    size_t length = 0;
    while (true) {
        char character;
        if (scanf("%c", &character) != 1) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (character == '\r' || character == '\n') {
            // Consume the newline sent after the command character before reading the value.
            if (length == 0) continue;
            break;
        }
        if (character == '\b' || character == 0x7f) {
            if (length > 0) {
                --length;
                printf("\b \b");
            }
            continue;
        }
        if (character >= '0' && character <= '9' && length < sizeof(input) - 1) {
            input[length++] = character;
            putchar(character);
        }
    }
    putchar('\n');

    char *end = NULL;
    unsigned long long seconds = strtoull(input, &end, 10);
    if (*input == '\0' || *end != '\0' || seconds == 0 || seconds > UINT32_MAX) {
        ESP_LOGE(TAG, "Invalid wake-up time: %s", input);
        return;
    }

    ESP_LOGW(TAG, "Setting timed wake-up to %llu second(s); entering low-power mode now", seconds);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_err_t ret = yoke_zxacc_maker_set_timer_wakeup(&s_zxacc_maker, (uint32_t)seconds);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ZXACC-maker timed wake-up command failed: %s", esp_err_to_name(ret));
    }
}



/* ============================ Touch UI ============================ */

static void ec11_init(void);
static void ec11_deinit(void);
static bool ec11_is_ready(void);
static void motor_init(void);
static void motor_deinit(void);
static bool motor_is_ready(void);
static void motor_drive(bool forward);
static void motor_stop(bool brake);
static void motor_set_speed(uint8_t percent);
static void radar_init(void);
static void radar_deinit(void);
static void radar_read(void);
static void radar_set_enabled(bool enabled);

static void ui_show_page(ui_page_t page)
{
    if (page >= UI_PAGE_MAX) return;
    s_current_page = page;
    for (ui_page_t i = 0; i < UI_PAGE_MAX; ++i) {
        if (i == page) {
            lv_obj_remove_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_status_label != NULL) {
        if (page == UI_PAGE_DASHBOARD) {
            lv_obj_remove_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_back_click(lv_event_t *event)
{
    (void)event;
    ui_show_page(UI_PAGE_DASHBOARD);
}

static void ui_page_gesture(lv_event_t *event)
{
    (void)event;
    if (s_current_page != UI_PAGE_DASHBOARD &&
        lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
        ui_show_page(UI_PAGE_DASHBOARD);
    }
}

static void ui_port_button_click(lv_event_t *event)
{
    ui_if_port_t port = (ui_if_port_t)(intptr_t)lv_event_get_user_data(event);
    const char *owner = NULL;
    if (if_port_is_occupied(port, &owner) && port != s_global_port) {
        ESP_LOGW(TAG, "%s is occupied by %s", if_port_get_config(port)->name,
                 owner ? owner : "another module");
        return;
    }

    s_global_port = port;

    const ui_if_port_config_t *selected = if_port_get_config(s_global_port);
    ESP_LOGI(TAG, "Global port selected: %s (%d/%d)",
             selected->name, selected->pin_a, selected->pin_b);
    ESP_LOGW(TAG, "New port applies to next init of EC11/Radar/Motor/ZXACC");

    ui_refresh_port_selector();
}

static lv_obj_t *ui_page_container_create(lv_obj_t *screen)
{
    lv_obj_t *cont = lv_obj_create(screen);
    lv_obj_set_size(cont, 240, 240);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(cont, ui_page_gesture, LV_EVENT_GESTURE, NULL);
    return cont;
}

static lv_obj_t *ui_make_button(lv_obj_t *parent, const char *text, lv_color_t bg,
                                lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, width, height);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(bg, 40), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    return btn;
}

static lv_obj_t *ui_make_dot(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_pos(dot, x, y);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    return dot;
}

static lv_obj_t *ui_title_bar_create(lv_obj_t *parent, const char *title)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 224, 38);
    lv_obj_set_pos(bar, 8, 6);
    lv_obj_set_style_bg_color(bar, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_radius(bar, 12, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);

    lv_obj_t *back = ui_make_button(bar, "<", UI_COLOR_CARD, 34, 28);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_add_event_cb(back, ui_back_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(bar);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 48, 0);
    return bar;
}

/* -------------------- Dashboard -------------------- */

typedef struct {
    const char *name;
    const char *icon;
} ui_dash_item_t;

static const ui_dash_item_t s_dash_items[] = {
    { "Encoder", "EC11" },
    { "Light",   "RGBW" },
    { "Radar",   "RAD" },
    { "Motor",   "MOTO" },
    { "System",  "PWR" },
    { "Key",     "KEY" },
};

static void ui_card_click(lv_event_t *event)
{
    ui_page_t page = (ui_page_t)(intptr_t)lv_event_get_user_data(event);
    ui_show_page(page);
}

static void ui_page_dashboard_create(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "RootMaker");
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(title, 12, 10);

    lv_obj_t *subtitle = lv_label_create(parent);
    lv_label_set_text(subtitle, "Hardware control");
    lv_obj_set_style_text_color(subtitle, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(subtitle, 12, 31);

    s_port_selector_box = lv_obj_create(parent);
    lv_obj_set_size(s_port_selector_box, 216, 42);
    lv_obj_set_pos(s_port_selector_box, 12, 54);
    lv_obj_set_style_bg_color(s_port_selector_box, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_radius(s_port_selector_box, 12, 0);
    lv_obj_set_style_border_width(s_port_selector_box, 0, 0);
    lv_obj_set_style_pad_all(s_port_selector_box, 0, 0);

    lv_obj_t *port_cap = lv_label_create(s_port_selector_box);
    lv_label_set_text(port_cap, "Interface");
    lv_obj_set_style_text_color(port_cap, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_style_text_font(port_cap, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(port_cap, 10, 13);


    for (int i = 0; i < UI_IF_PORT_MAX; ++i) {
        lv_obj_t *btn = ui_make_button(s_port_selector_box, s_if_port_configs[i].name,
                                       UI_COLOR_CARD_ON, 34, 28);
        lv_obj_set_pos(btn, 67 + i * 35, 7);
        lv_obj_set_style_text_font(lv_obj_get_child(btn, 0), &lv_font_montserrat_14, 0);
        lv_obj_add_event_cb(btn, ui_port_button_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        s_port_selector_buttons[i] = btn;
    }
    ui_refresh_port_selector();

    for (int i = 0; i < 6; ++i) {
        int row = i / 2;
        int col = i % 2;

        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_size(card, 104, 38);
        lv_obj_set_pos(card, 12 + col * 112, 106 + row * 39);
        lv_obj_set_style_bg_color(card, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(card, UI_COLOR_CARD_ON, LV_STATE_PRESSED);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 0, 0);

        lv_obj_t *icon = lv_label_create(card);
        lv_label_set_text(icon, s_dash_items[i].icon);
        lv_obj_set_style_text_color(icon, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(icon, 8, 5);

        lv_obj_t *name = lv_label_create(card);
        lv_label_set_text(name, s_dash_items[i].name);
        lv_obj_set_style_text_color(name, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(name, 8, 21);

        s_dash_dots[i] = ui_make_dot(card, 88, 7, UI_COLOR_CARD);
        s_dash_state_labels[i] = lv_label_create(card);
        lv_label_set_text(s_dash_state_labels[i], "");
        lv_obj_set_style_text_color(s_dash_state_labels[i], UI_COLOR_SUBTEXT, 0);
        lv_obj_set_style_text_font(s_dash_state_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_pos(s_dash_state_labels[i], 40, 5);

        lv_obj_add_event_cb(card, ui_card_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)(i + 1));
    }
}

static void ui_update_dash_dots(void)
{
    const bool states[6] = {
        s_ec11_initialized,
        s_rgbw_initialized,
        s_radar_initialized,
        s_motor.initialized,
        s_zxacc_maker.initialized,
        s_keyw.initialized,
    };
    for (int i = 0; i < 6; ++i) {
        lv_obj_set_style_bg_color(s_dash_dots[i],
                                  states[i] ? UI_COLOR_SUCCESS : UI_COLOR_CARD, 0);
        // lv_label_set_text(s_dash_state_labels[i], states[i] ? "ready" : "offline");
        lv_obj_set_style_text_color(s_dash_state_labels[i],
                                    states[i] ? UI_COLOR_SUCCESS : UI_COLOR_SUBTEXT, 0);
    }
}

/* -------------------- EC11 page -------------------- */

static const yoke_ec11_rgb_t s_ec11_colors[5] = {
    { .red = 255, .green = 0,   .blue = 0   },
    { .red = 0,   .green = 255, .blue = 0   },
    { .red = 0,   .green = 0,   .blue = 255 },
    { .red = 255, .green = 255, .blue = 255 },
    { .red = 0,   .green = 0,   .blue = 0   },
};

static void ec11_color_click(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if (!ec11_is_ready()) return;
    const yoke_ec11_rgb_t color = s_ec11_colors[index];
    if (yoke_ec11_set_ws2812_all(&s_ec11, color, color) != ESP_OK) {
        ESP_LOGE(TAG, "EC11 LED set failed");
    }
}

static void ec11_init_click(lv_event_t *event)
{
    (void)event;
    ec11_init();
}

static void ec11_deinit_click(lv_event_t *event)
{
    (void)event;
    ec11_deinit();
}

static void ui_page_ec11_create(lv_obj_t *parent)
{
    ui_title_bar_create(parent, "Encoder");

    lv_obj_t *count_cap = lv_label_create(parent);
    lv_label_set_text(count_cap, "COUNT");
    lv_obj_set_style_text_color(count_cap, UI_COLOR_SUBTEXT, 0);
    lv_obj_align(count_cap, LV_ALIGN_TOP_MID, 0, 48);

    s_ec11_count_label = lv_label_create(parent);
    lv_label_set_text(s_ec11_count_label, "--");
    lv_obj_set_style_text_color(s_ec11_count_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_ec11_count_label, &lv_font_montserrat_24, 0);
    lv_obj_align(s_ec11_count_label, LV_ALIGN_TOP_MID, 0, 66);

    s_ec11_key_label = lv_label_create(parent);
    lv_label_set_text(s_ec11_key_label, "Key: --");
    lv_obj_set_style_text_color(s_ec11_key_label, UI_COLOR_SUBTEXT, 0);
    lv_obj_align(s_ec11_key_label, LV_ALIGN_TOP_MID, 0, 102);

    const char *color_labels[5] = { "R", "G", "B", "W", "Off" };
    const lv_color_t color_bgs[5] = {
        UI_COLOR_DANGER, UI_COLOR_SUCCESS, UI_COLOR_ACCENT, lv_color_hex(0x94A3B8), UI_COLOR_CARD
    };
    for (int i = 0; i < 5; ++i) {
        lv_obj_t *btn = ui_make_button(parent, color_labels[i], color_bgs[i], 66, 30);
        int row = i / 3;
        int col = i % 3;
        lv_obj_set_pos(btn, 18 + col * 70, 120 + row * 34);
        lv_obj_add_event_cb(btn, ec11_color_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }

    lv_obj_t *init_btn = ui_make_button(parent, "Init", UI_COLOR_ACCENT, 96, 30);
    lv_obj_set_pos(init_btn, 18, 202);
    lv_obj_add_event_cb(init_btn, ec11_init_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *deinit_btn = ui_make_button(parent, "Deinit", UI_COLOR_DANGER, 96, 30);
    lv_obj_set_pos(deinit_btn, 126, 202);
    lv_obj_add_event_cb(deinit_btn, ec11_deinit_click, LV_EVENT_CLICKED, NULL);
}

static void ui_refresh_ec11(void)
{
    if (!s_ec11_initialized) {
        lv_label_set_text(s_ec11_count_label, "--");
        lv_label_set_text(s_ec11_key_label, "Key: not ready");
        return;
    }

    int16_t count = 0;
    if (yoke_ec11_read_encoder_count(&s_ec11, &count) == ESP_OK) {
        lv_label_set_text_fmt(s_ec11_count_label, "%d", (int)count);
    }

    yoke_ec11_key_t key;
    if (yoke_ec11_read_key(&s_ec11, &key) == ESP_OK) {
        lv_label_set_text_fmt(s_ec11_key_label, "Key: %s | %u",
                              key.pressed ? "pressed" : "released",
                              (unsigned)key.press_count);
    }
}

/* -------------------- RGBW page -------------------- */

static uint8_t s_rgbw_r, s_rgbw_g, s_rgbw_b, s_rgbw_w;
static lv_obj_t *s_rgbw_sliders[4];
static lv_obj_t *s_rgbw_preview;
static lv_obj_t *s_rgbw_state_label;
static lv_obj_t *s_rgbw_preset_buttons[4];
static lv_obj_t *s_rgbw_start_button;
static lv_obj_t *s_rgbw_stop_button;

static void ui_refresh_rgbw(void)
{
    for (int i = 0; i < 4; ++i) {
        if (s_rgbw_sliders[i] != NULL) {
            if (s_rgbw_initialized) {
                lv_obj_remove_state(s_rgbw_sliders[i], LV_STATE_DISABLED);
            } else {
                lv_obj_add_state(s_rgbw_sliders[i], LV_STATE_DISABLED);
            }
        }
        if (s_rgbw_preset_buttons[i] != NULL) {
            if (s_rgbw_initialized) {
                lv_obj_remove_state(s_rgbw_preset_buttons[i], LV_STATE_DISABLED);
            } else {
                lv_obj_add_state(s_rgbw_preset_buttons[i], LV_STATE_DISABLED);
            }
        }
    }

    if (s_rgbw_start_button != NULL) {
        if (s_rgbw_initialized) lv_obj_add_state(s_rgbw_start_button, LV_STATE_DISABLED);
        else lv_obj_remove_state(s_rgbw_start_button, LV_STATE_DISABLED);
    }
    if (s_rgbw_stop_button != NULL) {
        if (s_rgbw_initialized) lv_obj_remove_state(s_rgbw_stop_button, LV_STATE_DISABLED);
        else lv_obj_add_state(s_rgbw_stop_button, LV_STATE_DISABLED);
    }
    if (s_rgbw_state_label != NULL) {
        lv_label_set_text(s_rgbw_state_label,
                          s_rgbw_initialized ? "Ready" : "Not initialized");
        lv_obj_set_style_text_color(s_rgbw_state_label,
                                    s_rgbw_initialized ? UI_COLOR_SUCCESS : UI_COLOR_WARNING, 0);
    }
}

static void rgbw_apply(void)
{
    if (s_rgbw_initialized) {
        rgbw_set_all(s_rgbw_r, s_rgbw_g, s_rgbw_b, s_rgbw_w);
    }
    lv_color_t preview = lv_color_make(s_rgbw_r, s_rgbw_g, s_rgbw_b);
    if (s_rgbw_r == 0 && s_rgbw_g == 0 && s_rgbw_b == 0 && s_rgbw_w > 0) {
        /* White channel: blend pure white toward the background by intensity */
        uint32_t k = s_rgbw_w;
        uint32_t v = (k * 240 + (255 - k) * 16) / 255;
        preview = lv_color_make((uint8_t)v, (uint8_t)v, (uint8_t)v);
    }
    lv_obj_set_style_bg_color(s_rgbw_preview, preview, 0);
}

static void rgbw_slider_changed(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    int channel = (int)(intptr_t)lv_event_get_user_data(event);
    uint8_t value = (uint8_t)lv_slider_get_value(slider);
    switch (channel) {
    case 0: s_rgbw_r = value; break;
    case 1: s_rgbw_g = value; break;
    case 2: s_rgbw_b = value; break;
    default: s_rgbw_w = value; break;
    }
    rgbw_apply();
}

static void rgbw_preset_click(lv_event_t *event)
{
    int preset = (int)(intptr_t)lv_event_get_user_data(event);
    switch (preset) {
    case 0: s_rgbw_r = 255; s_rgbw_g = 0;   s_rgbw_b = 0;   s_rgbw_w = 0; break;
    case 1: s_rgbw_r = 0;   s_rgbw_g = 255; s_rgbw_b = 0;   s_rgbw_w = 0; break;
    case 2: s_rgbw_r = 0;   s_rgbw_g = 0;   s_rgbw_b = 255; s_rgbw_w = 0; break;
    default: s_rgbw_r = 0;  s_rgbw_g = 0;   s_rgbw_b = 0;   s_rgbw_w = 0; break;
    }
    const uint8_t values[4] = { s_rgbw_r, s_rgbw_g, s_rgbw_b, s_rgbw_w };
    for (int i = 0; i < 4; ++i) {
        lv_slider_set_value(s_rgbw_sliders[i], values[i], LV_ANIM_OFF);
    }
    rgbw_apply();
}

static void rgbw_control_click(lv_event_t *event)
{
    int action = (int)(intptr_t)lv_event_get_user_data(event);
    if (action == 0) rgbw_init();
    if (action == 1) rgbw_deinit();
    ui_refresh_rgbw();
}

static void ui_page_rgbw_create(lv_obj_t *parent)
{
    ui_title_bar_create(parent, "RGBW Strip");

    s_rgbw_state_label = lv_label_create(parent);
    lv_obj_set_pos(s_rgbw_state_label, 12, 48);
    lv_obj_set_style_text_font(s_rgbw_state_label, &lv_font_montserrat_14, 0);

    s_rgbw_preview = lv_obj_create(parent);
    lv_obj_set_size(s_rgbw_preview, 72, 22);
    lv_obj_set_pos(s_rgbw_preview, 156, 46);
    lv_obj_set_style_bg_color(s_rgbw_preview, lv_color_black(), 0);
    lv_obj_set_style_radius(s_rgbw_preview, 6, 0);
    lv_obj_set_style_border_width(s_rgbw_preview, 0, 0);

    const char *channels[4] = { "R", "G", "B", "W" };
    const lv_color_t ch_colors[4] = {
        UI_COLOR_DANGER, UI_COLOR_SUCCESS, UI_COLOR_ACCENT, UI_COLOR_TEXT
    };
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, channels[i]);
        lv_obj_set_style_text_color(lbl, ch_colors[i], 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(lbl, 12, 78 + i * 25);

        lv_obj_t *slider = lv_slider_create(parent);
        lv_obj_set_size(slider, 174, 20);
        lv_obj_set_pos(slider, 42, 76 + i * 25);
        lv_slider_set_range(slider, 0, 255);
        lv_slider_set_value(slider, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, UI_COLOR_CARD, LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, ch_colors[i], LV_PART_INDICATOR);
        lv_obj_add_event_cb(slider, rgbw_slider_changed, LV_EVENT_VALUE_CHANGED,
                            (void *)(intptr_t)i);
        s_rgbw_sliders[i] = slider;
    }

    const char *presets[4] = { "Red", "Green", "Blue", "Off" };
    const lv_color_t preset_bgs[4] = {
        UI_COLOR_DANGER, UI_COLOR_SUCCESS, UI_COLOR_ACCENT, UI_COLOR_CARD
    };
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *btn = ui_make_button(parent, presets[i], preset_bgs[i], 50, 30);
        lv_obj_set_pos(btn, 12 + i * 54, 176);
        lv_obj_add_event_cb(btn, rgbw_preset_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        s_rgbw_preset_buttons[i] = btn;
    }

    s_rgbw_start_button = ui_make_button(parent, "Start", UI_COLOR_ACCENT, 100, 28);
    lv_obj_set_pos(s_rgbw_start_button, 16, 210);
    lv_obj_add_event_cb(s_rgbw_start_button, rgbw_control_click, LV_EVENT_CLICKED, (void *)0);

    s_rgbw_stop_button = ui_make_button(parent, "Stop", UI_COLOR_DANGER, 100, 28);
    lv_obj_set_pos(s_rgbw_stop_button, 124, 210);
    lv_obj_add_event_cb(s_rgbw_stop_button, rgbw_control_click, LV_EVENT_CLICKED, (void *)1);

    ui_refresh_rgbw();
}

/* -------------------- Radar page -------------------- */

static void radar_click(lv_event_t *event)
{
    int action = (int)(intptr_t)lv_event_get_user_data(event);
    if (action == 0) radar_init();
    if (action == 1) radar_deinit();
    if (action == 2) radar_read();
}

static void radar_enable_changed(lv_event_t *event)
{
    lv_obj_t *sw = lv_event_get_target(event);
    radar_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void ui_page_radar_create(lv_obj_t *parent)
{
    ui_title_bar_create(parent, "Radar");

    lv_obj_t *comm_cap = lv_label_create(parent);
    lv_label_set_text(comm_cap, "COMM");
    lv_obj_set_style_text_color(comm_cap, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_pos(comm_cap, 40, 48);

    s_radar_comm_dot = ui_make_dot(parent, 100, 51, UI_COLOR_CARD);
    s_radar_comm_label = lv_label_create(parent);
    lv_label_set_text(s_radar_comm_label, "--");
    lv_obj_set_style_text_color(s_radar_comm_label, UI_COLOR_TEXT, 0);
    lv_obj_set_pos(s_radar_comm_label, 120, 48);

    lv_obj_t *det_cap = lv_label_create(parent);
    lv_label_set_text(det_cap, "TARGET");
    lv_obj_set_style_text_color(det_cap, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_pos(det_cap, 40, 72);

    s_radar_detect_dot = ui_make_dot(parent, 100, 75, UI_COLOR_CARD);
    s_radar_detect_label = lv_label_create(parent);
    lv_label_set_text(s_radar_detect_label, "--");
    lv_obj_set_style_text_color(s_radar_detect_label, UI_COLOR_TEXT, 0);
    lv_obj_set_pos(s_radar_detect_label, 120, 72);

    for (int i = 0; i < 2; ++i) {
        s_radar_obj_labels[i] = lv_label_create(parent);
        lv_label_set_text(s_radar_obj_labels[i], "obj --");
        lv_obj_set_style_text_color(s_radar_obj_labels[i], UI_COLOR_SUBTEXT, 0);
        lv_obj_set_pos(s_radar_obj_labels[i], 40, 100 + i * 22);
    }

    lv_obj_t *sw_cap = lv_label_create(parent);
    lv_label_set_text(sw_cap, "Enable");
    lv_obj_set_style_text_color(sw_cap, UI_COLOR_TEXT, 0);
    lv_obj_set_pos(sw_cap, 40, 150);

    s_radar_enable_switch = lv_switch_create(parent);
    lv_obj_set_pos(s_radar_enable_switch, 120, 146);
    lv_obj_add_event_cb(s_radar_enable_switch, radar_enable_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *init_btn = ui_make_button(parent, "Init", UI_COLOR_ACCENT, 68, 36);
    lv_obj_set_pos(init_btn, 12, 184);
    lv_obj_add_event_cb(init_btn, radar_click, LV_EVENT_CLICKED, (void *)0);

    lv_obj_t *read_btn = ui_make_button(parent, "Read", UI_COLOR_CARD, 68, 36);
    lv_obj_set_pos(read_btn, 86, 184);
    lv_obj_add_event_cb(read_btn, radar_click, LV_EVENT_CLICKED, (void *)2);

    lv_obj_t *deinit_btn = ui_make_button(parent, "Deinit", UI_COLOR_DANGER, 68, 36);
    lv_obj_set_pos(deinit_btn, 160, 184);
    lv_obj_add_event_cb(deinit_btn, radar_click, LV_EVENT_CLICKED, (void *)1);
}

static void ui_refresh_radar(void)
{
    if (!s_radar_initialized) {
        lv_obj_set_style_bg_color(s_radar_comm_dot, UI_COLOR_CARD, 0);
        lv_obj_set_style_bg_color(s_radar_detect_dot, UI_COLOR_CARD, 0);
        lv_label_set_text(s_radar_comm_label, "not ready");
        lv_label_set_text(s_radar_detect_label, "--");
        lv_label_set_text(s_radar_obj_labels[0], "obj --");
        lv_label_set_text(s_radar_obj_labels[1], "obj --");
        return;
    }

    yoke_rad60_radar_data_t data;
    if (yoke_rad60_get_radar_data(&data) != ESP_OK) {
        return;
    }

    lv_obj_set_style_bg_color(s_radar_comm_dot,
                              data.is_comm_ok ? UI_COLOR_SUCCESS : UI_COLOR_DANGER, 0);
    lv_label_set_text(s_radar_comm_label, data.is_comm_ok ? "OK" : "FAIL");

    lv_obj_set_style_bg_color(s_radar_detect_dot,
                              data.is_detected ? UI_COLOR_SUCCESS : UI_COLOR_CARD, 0);
    lv_label_set_text(s_radar_detect_label, data.is_detected ? "detected" : "none");

    uint8_t object_count = data.radar_info.obj_num > 2U ? 2U : data.radar_info.obj_num;
    for (int i = 0; i < 2; ++i) {
        if (i < object_count) {
            const multitarget_obj_info_t *obj = &data.radar_info.obj[i];
            lv_label_set_text_fmt(s_radar_obj_labels[i], "obj%d  %3u cm  %3d deg  %3d cm/s",
                                  i + 1, (unsigned)obj->range_val, (int)obj->angle_val,
                                  (int)obj->velo_val);
        } else {
            lv_label_set_text(s_radar_obj_labels[i], "obj --");
        }
    }
}

/* -------------------- Motor page -------------------- */

static uint8_t s_motor_speed_pct = 50;
static bool s_motor_forward = true;
static bool s_motor_running = false;
static lv_obj_t *s_motor_speed_slider;

static void motor_speed_changed(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    s_motor_speed_pct = lv_slider_get_value(slider);
    motor_set_speed(s_motor_speed_pct);
}

static void motor_click(lv_event_t *event)
{
    int action = (int)(intptr_t)lv_event_get_user_data(event);
    if (action == 0) { motor_init(); return; }
    if (action == 1) { motor_deinit(); s_motor_running = false; return; }
    if (action == 2) { motor_drive(true);  s_motor_forward = true;  s_motor_running = true; return; }
    if (action == 3) { motor_drive(false); s_motor_forward = false; s_motor_running = true; return; }
    if (action == 4) { motor_stop(false);  s_motor_running = false; return; }
    if (action == 5) { motor_stop(true);   s_motor_running = false; return; }
}

static void ui_refresh_motor(void)
{
    if (!s_motor.initialized) {
        lv_label_set_text(s_motor_state_label, "not initialized");
        return;
    }
    if (!s_motor_running) {
        lv_label_set_text(s_motor_state_label, "stopped");
        return;
    }
    lv_label_set_text_fmt(s_motor_state_label, "%s  %u%%",
                          s_motor_forward ? "forward" : "reverse",
                          (unsigned)s_motor_speed_pct);
}

static void ui_page_motor_create(lv_obj_t *parent)
{
    ui_title_bar_create(parent, "Motor");

    lv_obj_t *init_btn = ui_make_button(parent, "Init", UI_COLOR_ACCENT, 96, 30);
    lv_obj_set_pos(init_btn, 18, 48);
    lv_obj_add_event_cb(init_btn, motor_click, LV_EVENT_CLICKED, (void *)0);

    lv_obj_t *deinit_btn = ui_make_button(parent, "Deinit", UI_COLOR_DANGER, 96, 30);
    lv_obj_set_pos(deinit_btn, 126, 48);
    lv_obj_add_event_cb(deinit_btn, motor_click, LV_EVENT_CLICKED, (void *)1);

    s_motor_state_label = lv_label_create(parent);
    lv_label_set_text(s_motor_state_label, "not initialized");
    lv_obj_set_style_text_color(s_motor_state_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_motor_state_label, &lv_font_montserrat_20, 0);
    lv_obj_align(s_motor_state_label, LV_ALIGN_TOP_MID, 0, 84);

    lv_obj_t *dir_cap = lv_label_create(parent);
    lv_label_set_text(dir_cap, "DIRECTION");
    lv_obj_set_style_text_color(dir_cap, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_pos(dir_cap, 18, 112);

    lv_obj_t *fwd_btn = ui_make_button(parent, "Forward", UI_COLOR_ACCENT, 95, 30);
    lv_obj_set_pos(fwd_btn, 18, 130);
    lv_obj_add_event_cb(fwd_btn, motor_click, LV_EVENT_CLICKED, (void *)2);

    lv_obj_t *rev_btn = ui_make_button(parent, "Reverse", UI_COLOR_CARD, 95, 30);
    lv_obj_set_pos(rev_btn, 125, 130);
    lv_obj_add_event_cb(rev_btn, motor_click, LV_EVENT_CLICKED, (void *)3);

    lv_obj_t *stop_cap = lv_label_create(parent);
    lv_label_set_text(stop_cap, "STOP");
    lv_obj_set_style_text_color(stop_cap, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_pos(stop_cap, 18, 166);

    lv_obj_t *coast_btn = ui_make_button(parent, "Coast", UI_COLOR_CARD, 95, 28);
    lv_obj_set_pos(coast_btn, 18, 184);
    lv_obj_add_event_cb(coast_btn, motor_click, LV_EVENT_CLICKED, (void *)4);

    lv_obj_t *brake_btn = ui_make_button(parent, "Brake", UI_COLOR_DANGER, 95, 28);
    lv_obj_set_pos(brake_btn, 125, 184);
    lv_obj_add_event_cb(brake_btn, motor_click, LV_EVENT_CLICKED, (void *)5);

    lv_obj_t *speed_cap = lv_label_create(parent);
    lv_label_set_text(speed_cap, "SPEED");
    lv_obj_set_style_text_color(speed_cap, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_pos(speed_cap, 18, 214);

    s_motor_speed_slider = lv_slider_create(parent);
    lv_obj_set_size(s_motor_speed_slider, 180, 18);
    lv_obj_set_pos(s_motor_speed_slider, 16, 232);
    lv_slider_set_range(s_motor_speed_slider, 0, 100);
    lv_slider_set_value(s_motor_speed_slider, s_motor_speed_pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_motor_speed_slider, UI_COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_motor_speed_slider, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_add_event_cb(s_motor_speed_slider, motor_speed_changed, LV_EVENT_VALUE_CHANGED, NULL);
}

/* -------------------- ZXACC page -------------------- */

static void zxacc_click(lv_event_t *event)
{
    int action = (int)(intptr_t)lv_event_get_user_data(event);
    if (action == 0) {
        esp_err_t ret = yoke_zxacc_maker_set_timer_wakeup(&s_zxacc_maker,
                                                           s_zxacc_shutdown_seconds);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "ZXACC-maker shutdown timer set to %lu seconds",
                     (unsigned long)s_zxacc_shutdown_seconds);
        } else {
            ESP_LOGE(TAG, "ZXACC-maker shutdown timer write failed: %s",
                     esp_err_to_name(ret));
        }
        return;
    }

    if (action == 1 && s_zxacc_shutdown_seconds > 10) {
        s_zxacc_shutdown_seconds -= 10;
    } else if (action == 2 && s_zxacc_shutdown_seconds < 3600) {
        s_zxacc_shutdown_seconds += 10;
    }
    lv_label_set_text_fmt(s_zxacc_shutdown_label, "%lu s",
                          (unsigned long)s_zxacc_shutdown_seconds);
}

static void ui_page_zxacc_create(lv_obj_t *parent)
{
    ui_title_bar_create(parent, "System");

    lv_obj_t *section = lv_label_create(parent);
    lv_label_set_text(section, "POWER MANAGEMENT");
    lv_obj_set_style_text_color(section, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_pos(section, 18, 48);

    s_zxacc_version_label = lv_label_create(parent);
    lv_label_set_text(s_zxacc_version_label, "Version: 0x----");
    lv_obj_set_style_text_color(s_zxacc_version_label, UI_COLOR_TEXT, 0);
    lv_obj_align(s_zxacc_version_label, LV_ALIGN_TOP_MID, 0, 68);

    s_zxacc_voltage_label = lv_label_create(parent);
    lv_label_set_text(s_zxacc_voltage_label, "--.-- V");
    lv_obj_set_style_text_color(s_zxacc_voltage_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_zxacc_voltage_label, &lv_font_montserrat_24, 0);
    lv_obj_align(s_zxacc_voltage_label, LV_ALIGN_TOP_MID, 0, 88);

    s_zxacc_charge_label = lv_label_create(parent);
    lv_label_set_text(s_zxacc_charge_label, "Charge: unavailable");
    lv_obj_set_style_text_color(s_zxacc_charge_label, UI_COLOR_SUBTEXT, 0);
    lv_obj_align(s_zxacc_charge_label, LV_ALIGN_TOP_MID, 0, 122);

    lv_obj_t *shutdown_cap = lv_label_create(parent);
    lv_label_set_text(shutdown_cap, "SHUTDOWN TIMER");
    lv_obj_set_style_text_color(shutdown_cap, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_pos(shutdown_cap, 18, 154);

    lv_obj_t *decrease_btn = ui_make_button(parent, "-", UI_COLOR_CARD, 46, 30);
    lv_obj_set_pos(decrease_btn, 18, 176);
    lv_obj_add_event_cb(decrease_btn, zxacc_click, LV_EVENT_CLICKED, (void *)1);

    s_zxacc_shutdown_label = lv_label_create(parent);
    lv_label_set_text(s_zxacc_shutdown_label, "0 s");
    lv_obj_set_style_text_color(s_zxacc_shutdown_label, UI_COLOR_TEXT, 0);
    lv_obj_set_width(s_zxacc_shutdown_label, 120);
    lv_obj_set_style_text_align(s_zxacc_shutdown_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_zxacc_shutdown_label, 60, 182);

    lv_obj_t *increase_btn = ui_make_button(parent, "+", UI_COLOR_CARD, 46, 30);
    lv_obj_set_pos(increase_btn, 176, 176);
    lv_obj_add_event_cb(increase_btn, zxacc_click, LV_EVENT_CLICKED, (void *)2);

    lv_obj_t *set_btn = ui_make_button(parent, "Set timer", UI_COLOR_ACCENT, 150, 30);
    lv_obj_set_pos(set_btn, 45, 212);
    lv_obj_add_event_cb(set_btn, zxacc_click, LV_EVENT_CLICKED, (void *)0);
}

static void ui_refresh_zxacc(void)
{
    if (!s_zxacc_maker.initialized) {
        lv_label_set_text(s_zxacc_version_label, "Version: 0x----");
        lv_label_set_text(s_zxacc_voltage_label, "--.-- V");
        lv_label_set_text(s_zxacc_charge_label, "Charge: unavailable");
        return;
    }

    uint16_t firmware_version = 0;
    uint32_t voltage_mv = 0;
    yoke_zxacc_maker_charge_state_t charge_state = YOKE_ZXACC_MAKER_CHARGE_UNKNOWN;
    esp_err_t version_ret = yoke_zxacc_maker_get_firmware_version(&s_zxacc_maker,
                                                                    &firmware_version);
    esp_err_t voltage_ret = yoke_zxacc_maker_get_battery_voltage(&s_zxacc_maker,
                                                                   &voltage_mv);
    yoke_zxacc_maker_get_charge_state(&s_zxacc_maker, &charge_state);

    if (version_ret == ESP_OK) {
        lv_label_set_text_fmt(s_zxacc_version_label, "Version: 0x%04X", firmware_version);
    }
    if (voltage_ret == ESP_OK) {
        lv_label_set_text_fmt(s_zxacc_voltage_label, "%lu.%03lu V",
                              (unsigned long)(voltage_mv / 1000U),
                              (unsigned long)(voltage_mv % 1000U));
    } else {
        lv_label_set_text(s_zxacc_voltage_label, "--.-- V");
    }
    const char *charge_str = "unknown";
    if (charge_state == YOKE_ZXACC_MAKER_CHARGE_CHARGING) charge_str = "charging";
    if (charge_state == YOKE_ZXACC_MAKER_CHARGE_NOT_CHARGING) charge_str = "not charging";
    if (charge_state == YOKE_ZXACC_MAKER_CHARGE_COMPLETE) charge_str = "complete";
    lv_obj_set_style_text_color(s_zxacc_charge_label,
                                (charge_state == YOKE_ZXACC_MAKER_CHARGE_CHARGING)
                                    ? UI_COLOR_SUCCESS : UI_COLOR_SUBTEXT, 0);
    lv_label_set_text_fmt(s_zxacc_charge_label, "Charge: %s", charge_str);

}

static void ui_update_status_bar(void)
{
    const ui_if_port_config_t *selected = if_port_get_config(s_global_port);
    const char *owner = NULL;
    bool occupied = if_port_is_occupied(s_global_port, &owner);

    if (if_port_conflicts_with_keyw(s_global_port) && s_keyw.initialized) {
        lv_label_set_text_fmt(s_status_label, "Port %s unavailable: KEYW uses GPIO17/18",
                              selected->name);
        lv_obj_set_style_text_color(s_status_label, UI_COLOR_WARNING, 0);
        return;
    }

    if (occupied && owner != NULL) {
        lv_label_set_text_fmt(s_status_label, "Port %s in use by %s", selected->name, owner);
        lv_obj_set_style_text_color(s_status_label, UI_COLOR_WARNING, 0);
        return;
    }

    lv_obj_set_style_text_color(s_status_label, UI_COLOR_SUBTEXT, 0);

    if (s_current_page == UI_PAGE_DASHBOARD && s_zxacc_maker.initialized) {
        uint32_t voltage_mv = 0;
        if (yoke_zxacc_maker_get_battery_voltage(&s_zxacc_maker, &voltage_mv) == ESP_OK) {
            // lv_label_set_text_fmt(s_status_label, "Port %s  |  Battery %.2fV", selected->name,
            //                       voltage_mv / 1000.0f);
            return;
        }
    }

    lv_label_set_text_fmt(s_status_label, "Port %s  |  Ready", selected->name);
}

/* -------------------- KEYW page -------------------- */

static void ui_refresh_keyw(void)
{
    uint8_t count;
    uint8_t head;
    portENTER_CRITICAL(&s_keyw_mux);
    count = s_keyw_event_count;
    head = s_keyw_event_head;
    portEXIT_CRITICAL(&s_keyw_mux);

    for (int i = 0; i < 4; ++i) {
        if (i < count) {
            uint8_t idx = (head - 1 - i + KEYW_LOG_MAX) % KEYW_LOG_MAX;
            button_event_t event = s_keyw_event_log[idx];
            lv_label_set_text_fmt(s_keyw_log_labels[i], "%u. %s",
                                  (unsigned)(count - i), keyw_event_to_string(event));
        } else {
            lv_label_set_text(s_keyw_log_labels[i], "");
        }
    }
}

static void keyw_led_changed(lv_event_t *event)
{
    s_keyw_led_on = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    if (s_keyw.initialized) {
        yoke_keyw_set_led(&s_keyw, s_keyw_led_on);
    }
}

static void keyw_click(lv_event_t *event)
{
    int action = (int)(intptr_t)lv_event_get_user_data(event);
    if (action == 0) keyw_init();
    if (action == 1) keyw_deinit();
}

static void ui_page_keyw_create(lv_obj_t *parent)
{
    ui_title_bar_create(parent, "Key");

    lv_obj_t *led_cap = lv_label_create(parent);
    lv_label_set_text(led_cap, "LED");
    lv_obj_set_style_text_color(led_cap, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_pos(led_cap, 18, 54);

    s_keyw_led_switch = lv_switch_create(parent);
    lv_obj_set_pos(s_keyw_led_switch, 18, 74);
    lv_obj_add_state(s_keyw_led_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_keyw_led_switch, keyw_led_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *log_cap = lv_label_create(parent);
    lv_label_set_text(log_cap, "EVENTS");
    lv_obj_set_style_text_color(log_cap, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_pos(log_cap, 18, 110);

    for (int i = 0; i < 4; ++i) {
        s_keyw_log_labels[i] = lv_label_create(parent);
        lv_label_set_text(s_keyw_log_labels[i], "");
        lv_obj_set_style_text_color(s_keyw_log_labels[i], UI_COLOR_TEXT, 0);
        lv_obj_set_pos(s_keyw_log_labels[i], 18, 130 + i * 18);
    }

    lv_obj_t *init_btn = ui_make_button(parent, "Start", UI_COLOR_ACCENT, 94, 30);
    lv_obj_set_pos(init_btn, 18, 204);
    lv_obj_add_event_cb(init_btn, keyw_click, LV_EVENT_CLICKED, (void *)0);

    lv_obj_t *stop_btn = ui_make_button(parent, "Stop", UI_COLOR_DANGER, 94, 30);
    lv_obj_set_pos(stop_btn, 128, 204);
    lv_obj_add_event_cb(stop_btn, keyw_click, LV_EVENT_CLICKED, (void *)1);
}

/* -------------------- Timers & entry -------------------- */

static void ui_timer_200ms(lv_timer_t *timer)
{
    (void)timer;
    if (s_current_page == UI_PAGE_EC11) ui_refresh_ec11();
    if (s_current_page == UI_PAGE_RGBW) ui_refresh_rgbw();
    if (s_current_page == UI_PAGE_MOTOR) ui_refresh_motor();
    if (s_current_page == UI_PAGE_KEYW) ui_refresh_keyw();
    ui_update_dash_dots();
    ui_refresh_port_selector();
}

static void ui_timer_500ms(lv_timer_t *timer)
{
    (void)timer;
    if (s_current_page == UI_PAGE_RADAR) ui_refresh_radar();
    if (s_current_page == UI_PAGE_ZXACC) ui_refresh_zxacc();
    ui_update_status_bar();
}

static void screen_create_ui(void)
{
    if (!lvgl_port_lock(0)) return;

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, UI_COLOR_BG, 0);

    for (ui_page_t i = 0; i < UI_PAGE_MAX; ++i) {
        s_pages[i] = ui_page_container_create(screen);
    }

    ui_page_dashboard_create(s_pages[UI_PAGE_DASHBOARD]);
    ui_page_ec11_create(s_pages[UI_PAGE_EC11]);
    ui_page_rgbw_create(s_pages[UI_PAGE_RGBW]);
    ui_page_radar_create(s_pages[UI_PAGE_RADAR]);
    ui_page_motor_create(s_pages[UI_PAGE_MOTOR]);
    ui_page_zxacc_create(s_pages[UI_PAGE_ZXACC]);
    ui_page_keyw_create(s_pages[UI_PAGE_KEYW]);

    s_status_label = lv_label_create(screen);
    lv_label_set_text(s_status_label, "RootMaker  Ready");
    lv_obj_set_style_text_color(s_status_label, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    ui_show_page(UI_PAGE_DASHBOARD);

    lv_timer_create(ui_timer_200ms, 200, NULL);
    lv_timer_create(ui_timer_500ms, 500, NULL);

    lvgl_port_unlock();
}

static bool motor_is_ready(void)
{
    if (!s_motor.initialized) {
        ESP_LOGW(TAG, "Motor is not initialized; input m first");
        return false;
    }
    return true;
}

static void motor_set_speed(uint8_t percent)
{
    if (!motor_is_ready()) return;

    esp_err_t ret = yoke_moto_set_speed_percent(&s_motor, percent);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Motor speed=%u%%", percent);
    } else {
        ESP_LOGE(TAG, "Motor speed set failed: %s", esp_err_to_name(ret));
    }
}

static void motor_init(void)
{
    if (s_motor.initialized) {
        ESP_LOGW(TAG, "Motor is already initialized");
        return;
    }

    const ui_if_port_config_t *port = if_port_get_config(s_global_port);

    if (if_port_conflicts_with_keyw(s_global_port) && s_keyw.initialized) {
        ESP_LOGE(TAG, "Motor init blocked: selected port %s conflicts with KEYW GPIO17/18",
                 port->name);
        return;
    }

    yoke_moto_config_t config = yoke_moto_default_config();
    config.pwma_gpio_num = port->pin_a;
    config.pwmb_gpio_num = port->pin_b;
    esp_err_t ret = yoke_moto_init(&s_motor, &config);
    if (ret == ESP_OK) {
        s_motor_port = s_global_port;
        ESP_LOGI(TAG, "Motor initialized on %s: PWMA=%d PWMB=%d, speed=50%%, coast",
                 port->name, port->pin_a, port->pin_b);
        return;
    }

    ESP_LOGE(TAG, "Motor init failed: %s", esp_err_to_name(ret));
    (void)yoke_moto_deinit(&s_motor);
}

static void motor_drive(bool forward)
{
    if (!motor_is_ready()) return;

    esp_err_t ret = forward ? yoke_moto_forward(&s_motor) : yoke_moto_reverse(&s_motor);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Motor %s", forward ? "forward" : "reverse");
    } else {
        ESP_LOGE(TAG, "Motor direction set failed: %s", esp_err_to_name(ret));
    }
}

static void motor_stop(bool brake)
{
    if (!motor_is_ready()) return;

    esp_err_t ret = brake ? yoke_moto_brake(&s_motor) : yoke_moto_coast(&s_motor);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Motor %s", brake ? "brake" : "coast");
    } else {
        ESP_LOGE(TAG, "Motor stop failed: %s", esp_err_to_name(ret));
    }
}

static void motor_deinit(void)
{
    if (!motor_is_ready()) return;

    esp_err_t ret = yoke_moto_deinit(&s_motor);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Motor deinitialized");
    } else {
        ESP_LOGE(TAG, "Motor deinit failed: %s", esp_err_to_name(ret));
    }
}

static bool ec11_is_ready(void)
{
    if (!s_ec11_initialized) {
        ESP_LOGW(TAG, "EC11 is not initialized; input I first");
        return false;
    }
    return true;
}

static void ec11_init(void)
{
    if (s_ec11_initialized) {
        ESP_LOGW(TAG, "EC11 is already initialized");
        return;
    }

    const ui_if_port_config_t *port = if_port_get_config(s_global_port);

    if (if_port_conflicts_with_keyw(s_global_port) && s_keyw.initialized) {
        ESP_LOGE(TAG, "EC11 init blocked: selected port %s conflicts with KEYW GPIO17/18",
                 port->name);
        return;
    }

    if (s_ec11_i2c_bus == NULL) {
        const i2c_master_bus_config_t bus_config = {
            .i2c_port = EC11_I2C_PORT,
            .sda_io_num = port->pin_a,
            .scl_io_num = port->pin_b,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        esp_err_t ret = i2c_new_master_bus(&bus_config, &s_ec11_i2c_bus);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "EC11 I2C bus init failed: %s", esp_err_to_name(ret));
            return;
        }
    }

    yoke_ec11_config_t config = yoke_ec11_default_config();
    config.verify_device_id = true;
    esp_err_t ret = yoke_ec11_init(&s_ec11, s_ec11_i2c_bus, &config);
    if (ret == ESP_OK) {
        s_ec11_initialized = true;
        s_ec11_port = s_global_port;
        ESP_LOGI(TAG, "EC11 initialized on %s: I2C%d SDA=%d SCL=%d",
                 port->name, EC11_I2C_PORT, port->pin_a, port->pin_b);
    } else {
        ESP_LOGE(TAG, "EC11 init failed: %s", esp_err_to_name(ret));
    }
}

static void ec11_read_key(void)
{
    if (!ec11_is_ready()) return;

    yoke_ec11_key_t key;
    esp_err_t ret = yoke_ec11_read_key(&s_ec11, &key);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "EC11 key: pressed=%d press_count=%u", key.pressed, key.press_count);
    } else {
        ESP_LOGE(TAG, "EC11 key read failed: %s", esp_err_to_name(ret));
    }
}

static void ec11_read_encoder(bool read_diff)
{
    if (!ec11_is_ready()) return;

    int16_t value;
    esp_err_t ret = read_diff ? yoke_ec11_read_encoder_diff(&s_ec11, &value)
                              : yoke_ec11_read_encoder_count(&s_ec11, &value);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "EC11 %s=%d", read_diff ? "diff" : "count", value);
    } else {
        ESP_LOGE(TAG, "EC11 %s read failed: %s", read_diff ? "diff" : "count",
                 esp_err_to_name(ret));
    }
}

static void ec11_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!ec11_is_ready()) return;

    const yoke_ec11_rgb_t color = {.red = red, .green = green, .blue = blue};
    esp_err_t ret = yoke_ec11_set_ws2812_all(&s_ec11, color, color);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EC11 LED set failed: %s", esp_err_to_name(ret));
    }
}

static void ec11_deinit(void)
{
    if (!ec11_is_ready()) return;

    esp_err_t ret = yoke_ec11_deinit(&s_ec11);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EC11 deinit failed: %s", esp_err_to_name(ret));
        return;
    }
    s_ec11_initialized = false;

    ret = i2c_del_master_bus(s_ec11_i2c_bus);
    if (ret == ESP_OK) {
        s_ec11_i2c_bus = NULL;
        ESP_LOGI(TAG, "EC11 deinitialized");
    } else {
        ESP_LOGE(TAG, "EC11 I2C bus deinit failed: %s", esp_err_to_name(ret));
    }
}

static void radar_init(void)
{
    if (s_radar_initialized) {
        ESP_LOGW(TAG, "RAD60 is already initialized");
        return;
    }

    const ui_if_port_config_t *port = if_port_get_config(s_global_port);

    if (if_port_conflicts_with_keyw(s_global_port) && s_keyw.initialized) {
        ESP_LOGE(TAG, "RAD60 init blocked: selected port %s conflicts with KEYW GPIO17/18",
                 port->name);
        return;
    }

    esp_err_t ret = yoke_rad60_init(RADAR_UART_NUM, port->pin_a, port->pin_b,
                                    RADAR_BAUDRATE);
    if (ret == ESP_OK) {
        s_radar_initialized = true;
        s_radar_port = s_global_port;
        ESP_LOGI(TAG, "RAD60 initialized on %s: UART%d TX=%d RX=%d baud=%lu", port->name,
                 RADAR_UART_NUM, port->pin_a, port->pin_b, (unsigned long)RADAR_BAUDRATE);
    } else {
        ESP_LOGE(TAG, "RAD60 init failed: %s", esp_err_to_name(ret));
    }
}

static void radar_read(void)
{
    if (!s_radar_initialized) {
        ESP_LOGW(TAG, "RAD60 is not initialized; input i first");
        return;
    }

    yoke_rad60_radar_data_t data;
    esp_err_t ret = yoke_rad60_get_radar_data(&data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RAD60 read failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "RAD60: comm=%d detected=%d objects=%u", data.is_comm_ok,
             data.is_detected, data.radar_info.obj_num);
    uint8_t object_count = data.radar_info.obj_num > 2U ? 2U : data.radar_info.obj_num;
    for (uint8_t index = 0; index < object_count; ++index) {
        const multitarget_obj_info_t *object = &data.radar_info.obj[index];
        ESP_LOGI(TAG, "  object[%u]: distance=%u cm angle=%d deg velocity=%d cm/s id=%u",
                 index, object->range_val, object->angle_val, object->velo_val, object->objid);
    }
}

static void radar_set_enabled(bool enabled)
{
    if (!s_radar_initialized) {
        ESP_LOGW(TAG, "RAD60 is not initialized; input i first");
        return;
    }

    esp_err_t ret = yoke_rad60_set_radar_enable(enabled ? 1U : 0U);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "RAD60 detection %s", enabled ? "enabled" : "disabled");
    } else {
        ESP_LOGE(TAG, "RAD60 enable command failed: %s", esp_err_to_name(ret));
    }
}

static void radar_deinit(void)
{
    if (!s_radar_initialized) {
        ESP_LOGW(TAG, "RAD60 is not initialized");
        return;
    }

    esp_err_t ret = yoke_rad60_deinit();
    if (ret == ESP_OK) {
        s_radar_initialized = false;
        ESP_LOGI(TAG, "RAD60 deinitialized");
    } else {
        ESP_LOGE(TAG, "RAD60 deinit failed: %s", esp_err_to_name(ret));
    }
}

void app_main(void)
{
    // The power baseboard creates system I2C1 on GPIO41/40 before any optional display.
    zxacc_maker_init();

    if (screen_with_lvgl_init() != NULL) {
        screen_create_ui();
    } else {
        ESP_LOGE(TAG, "Screen initialization failed; continuing without UI");
    }

    char input;
    while(1){
        vTaskDelay(pdMS_TO_TICKS(10));
        if (scanf("%c", &input) == 1){
            if (input == '\r' || input == '\n') {
                continue;
            }
            ESP_LOGI(TAG, "input: %c", input);
            if(input == 'a'){
                //读取zxacc_maker各个状态
                uint16_t version = 0;
                yoke_zxacc_maker_get_firmware_version(&s_zxacc_maker, &version);
                ESP_LOGI(TAG, "ZXACC Maker 版本: 0x%04X", version);
                //读取电池电压
                uint32_t voltage_mv = 0;
                yoke_zxacc_maker_get_battery_voltage(&s_zxacc_maker, &voltage_mv);
                ESP_LOGI(TAG, "ZXACC Maker 电压: %u mV", voltage_mv);
                //读取充电状态
                yoke_zxacc_maker_charge_state_t charge_state = 0;
                yoke_zxacc_maker_get_charge_state(&s_zxacc_maker, &charge_state);
                ESP_LOGI(TAG, "ZXACC Maker 充电状态: %d", charge_state);
                //读取唤醒时间和关机时间
                uint16_t wake_time = 0;
                uint16_t shutdown_time = 0;
                yoke_zxacc_maker_get_wakeup_time(&s_zxacc_maker, &wake_time);
                yoke_zxacc_maker_get_shutdown_time(&s_zxacc_maker, &shutdown_time);
                ESP_LOGI(TAG, "ZXACC Maker wakeup time: %u s, shutdown time: %u s", wake_time, shutdown_time);
                
            } else if(input == 'b'){
                //关机
                ESP_LOGW(TAG, "关机");
                yoke_zxacc_maker_shutdown(&s_zxacc_maker);
            } else if(input == 'c'){
                //设置关机10秒
                yoke_zxacc_maker_set_timer_wakeup(&s_zxacc_maker, 10);
            } else if(input == 'd'){
                
            } else if(input == 'e'){
            } else if(input == 'f'){
            } else if(input == 'g'){
            } else if(input == 'h'){
            }
        }
    }
}
