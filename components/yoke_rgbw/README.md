# Yoke-RGBW

RGBW WS2812 灯带的 ESP-IDF 组件，使用 RMT 发送 GRBW 数据流。

```cmake
idf_component_register(SRCS "main.c" REQUIRES yoke_rgbw)
```

```c
yoke_rgbw_config_t config = yoke_rgbw_default_config();
config.gpio_num = GPIO_NUM_13;
config.led_num = 4;
ESP_ERROR_CHECK(yoke_rgbw_init(&config));
ESP_ERROR_CHECK(yoke_rgbw_set_all(0, 255, 255, 0));
```

`yoke_rgbw_set_all()` 的参数顺序是红、绿、蓝、白；组件按 WS2812 RGBW 常见的 GRBW 顺序发送数据。
