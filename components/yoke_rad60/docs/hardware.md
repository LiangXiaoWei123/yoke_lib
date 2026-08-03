# 硬件说明

Yoke-RAD60 使用 UART 通信。默认参数为 UART1、TX=GPIO15、RX=GPIO16、921600 bit/s；这些仅是源驱动的默认值，实际接线请按目标硬件传给 `yoke_rad60_init()`。

ESP32 的 TX 应接模块 RX，ESP32 的 RX 应接模块 TX，且两端必须共地。请根据模块供电规格选择 3.3 V 或经电平转换后的 UART 信号。
