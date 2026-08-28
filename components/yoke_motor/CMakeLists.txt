idf_component_register(
    SRCS "src/yoke_motor.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio rootmaker_utils
    PRIV_REQUIRES espressif__bdc_motor
)
