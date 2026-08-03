# Yoke-RAD60

Yoke-RAD60 雷达模块的 ESP-IDF UART 组件。源驱动中的 `at6010_*` API 已统一更名为 `yoke_rad60_*`，组件不再依赖原工程的 `qmsd_utils`。

```cmake
idf_component_register(SRCS "main.c" REQUIRES yoke_rad60)
```

```c
ESP_ERROR_CHECK(yoke_rad60_init(UART_NUM_1, GPIO_NUM_15, GPIO_NUM_16, 921600));

yoke_rad60_radar_data_t data;
ESP_ERROR_CHECK(yoke_rad60_get_radar_data(&data));
```

初始化后，组件会启动 UART 接收任务并缓存上报帧。`yoke_rad60_get_radar_data()` 从缓存读取最新检测结果，不会阻塞等待下一帧。

默认串口参数在 [公开头文件](include/yoke_rad60.h) 中以 `YOKE_RAD60_*_DEFAULT` 常量提供。
