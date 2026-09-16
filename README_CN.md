# RootMaker ESP-IDF 组件库

[English](README.md) | [中文](README_CN.md)

本仓库是 RootMaker 系列外设的 ESP-IDF 组件集合。每个外设独立封装为一个组件，
组件内自带 `examples/` 示例工程，所有组件都可独立发布到
[ESP 组件库](https://components.espressif.com/)。

根目录的 `main` 是一个基于 LVGL 的触屏联调示例，用于测试各个外设组件。

## 组件列表

| 组件 | 功能 | 发布名 |
| --- | --- | --- |
| `rootmaker_utils` | 共用接口模型与 I2C 总线池 | `ryzobeezero/rootmaker_utils` |
| `yoke_ec11` | EC11 编码器、按键与板载两颗 WS2812 | `ryzobeezero/yoke_ec11` |
| `yoke_keyw` | 按键 + GPIO 指示灯模块 | `ryzobeezero/yoke_keyw` |
| `yoke_motor` | 有刷直流电机（MCPWM） | `ryzobeezero/yoke_motor` |
| `yoke_rgbw` | 外接 RGBW WS2812 灯带 | `ryzobeezero/yoke_rgbw` |
| `yoke_rad60` | RAD60 UART 雷达模块 | `ryzobeezero/yoke_rad60` |
| `yoke_screen` | ST7789 屏幕 + CST816T 触摸 + LVGL | `ryzobeezero/yoke_screen` |
| `yoke_zxacc_maker` | ZXACC-maker PY32 电源管理底板 | `ryzobeezero/yoke_zxacc_maker` |

每个组件的目录结构：

```text
components/<name>/
├── CMakeLists.txt        # 组件构建定义
├── idf_component.yml     # 版本、依赖与组件库元信息（上传时读取）
├── Kconfig               # 组件配置项（如适用）
├── README.md
├── CHANGELOG.md
├── LICENSE
├── include/              # 公共头文件
├── src/                  # 实现源码
└── examples/             # 示例工程（随组件一起上传）
```

## 引脚默认值

| 功能 | 默认引脚 |
| --- | --- |
| EC11 I2C | SDA=GPIO15，SCL=GPIO16 |
| RGBW 灯带 | GPIO13，4 颗灯珠 |
| RAD60 UART | UART1，TX=GPIO17，RX=GPIO18，921600 bit/s |
| ZXACC-maker I2C | SDA=GPIO41，SCL=GPIO40，地址 `0x73` |

> 雷达原驱动默认使用 GPIO15/16，与 EC11 的 I2C 冲突，因此根工程控制台把雷达
> 默认值改为 GPIO17/18。各组件示例中可通过宏或入参覆盖默认引脚。

## 发布到 ESP 组件库

`.github/workflows/upload-components.yml` 使用
[`espressif/upload-components-ci-action`](https://github.com/espressif/upload-components-ci-action)
一次性上传全部 8 个组件到 `ryzobeezero` 命名空间：

1. 需要发布新版本时，先修改对应组件 `idf_component.yml` 中的 `version`（SemVer）。
2. 提交并推送，然后打一个 SemVer 格式的标签（如 `v1.0.0`）：

   ```sh
   git tag v1.0.0
   git push origin v1.0.0
   ```

3. GitHub Actions 触发上传。组件版本取自各自的 `idf_component.yml`，版本已存在于
   组件库的组件会被自动跳过，因此一个标签可以只发布被改动的组件。

首次发布前，需要在 [components.espressif.com](https://components.espressif.com/)
完成 `ryzobeezero` namespace 的注册与登录（GitHub OAuth），并保证 GitHub 账号对
该 namespace 有写权限。

## 构建与烧录（根联调工程）

加载 ESP-IDF 环境后执行：

```sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

各组件独立使用时可参考其 `examples/` 下的示例工程。

## 硬件文档

- [ZXRoot-Maker-V10 原理图](docs/ZXRoot-Maker-V10.pdf)
- [ZXACC-Maker-V10 原理图](docs/ZXACC-Maker-V10.pdf)
