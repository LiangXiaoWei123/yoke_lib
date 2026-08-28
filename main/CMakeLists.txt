idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES rootmaker_utils yoke_ec11 yoke_keyw yoke_motor yoke_rgbw yoke_rad60 yoke_screen yoke_zxacc_maker freertos
)
