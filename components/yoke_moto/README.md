# Yoke-Moto

基于 MCPWM 与 `espressif__bdc_motor` 的双 PWM 直流电机组件，适用于需要 PWMA/PWMB 输入的 H 桥驱动器。

```c
yoke_moto_t moto = {0};
yoke_moto_config_t config = yoke_moto_default_config();
config.pwma_gpio_num = GPIO_NUM_17;
config.pwmb_gpio_num = GPIO_NUM_18;
ESP_ERROR_CHECK(yoke_moto_init(&moto, &config));
ESP_ERROR_CHECK(yoke_moto_forward(&moto));
ESP_ERROR_CHECK(yoke_moto_set_speed_percent(&moto, 50));
```

初始化完成后电机保持 coast 停止，只有调用 `yoke_moto_forward()` 或 `yoke_moto_reverse()` 才会转动。
