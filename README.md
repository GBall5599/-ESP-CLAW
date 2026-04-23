# ESP-CLAW 移植项目

将 [esp-claw](https://github.com/espressif/esp-claw)（乐鑫 AI Agent 框架）移植到**立创实战派 ESP32-S3 开发板**。

---

## 项目目标

在立创实战派 ESP32-S3 开发板上运行 esp-claw，实现：
- 通过 IM 聊天（Telegram/微信/飞书/QQ）控制设备
- AI Agent 动态生成 Lua 脚本控制硬件外设
- 结构化记忆系统（长期记忆、会话历史）
- 设备同时作为 MCP Server/Client

## 硬件平台

| 项目 | 规格 |
|------|------|
| **开发板** | 立创实战派 ESP32-S3 (SZP) |
| **芯片** | ESP32-S3-WROOM-1-N16R8 |
| **Flash** | 16MB |
| **PSRAM** | 8MB (Octal, 80MHz) |
| **显示屏** | ST7789 2.0寸 320x240 IPS (SPI) |
| **触摸屏** | FT6336 电容触摸 (I2C) |
| **摄像头** | GC0308 (DVP 接口) |
| **音频 DAC** | ES8311 (I2C + I2S) |
| **音频 ADC** | ES7210 4通道 (I2C + I2S) |
| **IMU** | QMI8658 六轴 (I2C) |
| **IO 扩展器** | PCA9557 (I2C) — 控制 LCD_CS/PA_EN/DVP_PWDN |
| **SD 卡** | SDMMC 1-bit 模式 |
| **按键** | BOOT 键 (GPIO0) |

> 硬件详情：https://wiki.lckfb.com/zh-hans/szpi-esp32s3/

## 软件版本

| 项目 | 版本 |
|------|------|
| **ESP-IDF** | v5.5.1 |
| **LVGL** | 8.3.x |
| **esp-claw** | 上游 main 分支 |

## 参考项目

| 项目 | 说明 | 路径 |
|------|------|------|
| [espressif/esp-claw](https://github.com/espressif/esp-claw) | 乐鑫 AI Agent 框架（移植目标） | `Ref/` |
| 立创实战派 ESP32-S3 例程 | 开发板官方例程（板级驱动参考） | `E:\资料\立创·实战派ESP32-S3开发板资料\01-例程\` |

## 目录结构

```
ESPCLAW/
├── README.md                    # 本文件
├── Ref/                         # esp-claw 上游源码（参考）
│   └── 项目解析.md               # esp-claw 项目完整解析文档
├── 14-handheld/                  # 立创实战派原始例程 (IDF v5.4.3)
└── 14-handheld(new)/            # 已适配 IDF v5.5.1 的例程（添加了中文注释）
```

## 移植计划

| 阶段 | 内容 | 状态 |
|------|------|------|
| **一、环境准备** | 安装 IDF v5.5.1，验证开发板例程可编译 | 进行中 |
| **二、板级适配** | 在 esp-claw 的 boards/ 下新建 esp32_s3_szp 板级定义 | 待开始 |
| **三、编译调通** | 首次编译 esp-claw (szp 板)，解决编译错误 | 待开始 |
| **四、外设逐项启用** | 显示屏→触摸→音频→摄像头→SD卡→IMU | 待开始 |
| **五、集成测试** | 完整 Agent + IM + Lua + 记忆系统验证 | 待开始 |

## 开发环境

- **IDE**: VS Code + ESP-IDF 插件
- **IDF 路径**: `E:\ESPIDF\551\v5.5.1\esp-idf`
- **工具链路径**: `E:\ESPIDF\551\Tools_551`
- **GitHub**: https://github.com/GBall5599/-ESP-CLAW
