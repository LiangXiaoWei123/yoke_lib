idf_component_register(
    SRCS "src/yoke_moto.c"
    INCLUDE_DIRS "include"
    REQUIRES espressif__bdc_motor esp_driver_gpio
)
