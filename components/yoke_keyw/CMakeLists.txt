idf_component_register(
    SRCS "src/yoke_keyw.c"
    INCLUDE_DIRS "include"
    REQUIRES espressif__button esp_driver_gpio esp_driver_ledc
)
