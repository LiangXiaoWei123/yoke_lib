idf_component_register(
    SRCS  "src/yoke_ec11/yoke_ec11.c"
         "src/yoke_keyw/yoke_keyw.c" "src/yoke_moto/yoke_moto.c"
         "src/yoke_rgbw/yoke_rgbw.c" "src/yoke_screen/yoke_screen.c"
         "src/yoke_rad60/yoke_rad60.c" "src/yoke_rad60/yoke_rad60_protocol.c"
         "src/yoke_zxacc_maker/yoke_zxacc_maker.c"
         "src/yoke_screen/lcd_panel_st7789_spec.c"
    INCLUDE_DIRS "src" "src/yoke_ec11" "src/yoke_keyw" "src/yoke_moto" "src/yoke_rgbw" "src/yoke_rad60" "src/yoke_screen"
    REQUIRES espressif__button espressif__bdc_motor esp_driver_i2c esp_driver_rmt esp_driver_spi esp_driver_uart esp_lcd esp_lvgl_port lvgl__lvgl freertos driver
)
