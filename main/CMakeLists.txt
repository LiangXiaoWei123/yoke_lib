idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES yoke_ec11 yoke_rad60 yoke_rgbw esp_driver_i2c freertos
)
