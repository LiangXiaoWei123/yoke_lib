# 硬件说明

`pwma_gpio_num` 与 `pwmb_gpio_num` 分别连接 H 桥的 PWMA、PWMB 输入。ESP32 与电机驱动器必须共地；电机电源应由独立、满足电流要求的电源提供，不能直接从 ESP32 GPIO 供电。
