# ESP-CLAW 移植项目

将 [esp-claw](https://github.com/espressif/esp-claw)（乐鑫 AI Agent 框架）移植到**立创实战派 ESP32-S3 开发板**。

---

## 项目状态

| 阶段 | 内容 | 状态 |
|------|------|------|
| **一、环境准备** | 安装 IDF v5.5.1，适配例程到新 I2C 驱动 | 已完成 |
| **二、板级适配** | 创建 SZP 板级定义（YAML + setup_device.c） | 已完成 |
| **三、编译调通** | 首次编译通过，解决依赖/编码/CONFIG 问题 | 已完成 |
| **四、运行验证** | WiFi/LLM/LCD/IM 全部验证，SPI DMA 优化待烧录 | 进行中 |

---

## 硬件平台

| 项目 | 规格 |
|------|------|
| **开发板** | 立创实战派 ESP32-S3 (SZP) |
| **芯片** | ESP32-S3-WROOM-1-N16R8 |
| **Flash** | 16MB |
| **PSRAM** | 8MB (Octal, 80MHz) |
| **显示屏** | ST7789 2.0寸 320x240 IPS (SPI) |
| **触摸屏** | FT5x06 电容触摸 (I2C) |
| **摄像头** | GC0308 (DVP 接口) |
| **音频 DAC** | ES8311 (I2C + I2S) |
| **音频 ADC** | ES7210 4通道 (I2C + I2S) |
| **IMU** | QMI8658 六轴 (I2C) |
| **IO 扩展器** | PCA9557 (I2C) — 控制 LCD_CS/PA_EN/DVP_PWDN |
| **SD 卡** | SDMMC 1-bit 模式 |

> 硬件详情：https://wiki.lckfb.com/zh-hans/szpi-esp32s3/

## 已验证功能

- WiFi APSTA + Web 配网 + 扫描热切换
- LLM Agent (DeepSeek-v4-flash) + 网页聊天 (中英文)
- QQ 机器人 IM 通道
- LCD 表情动画 (emote) + Lua 脚本绘制（文本/图片）
- Display Arbiter (Emote/Lua 所有权切换 + 自动恢复)
- 摄像头/音频编解码器/触摸屏/SD卡/IMU 硬件初始化
- 17 个 Lua 模块 + 17 个能力组注册
- MCP Server + mDNS

## 目录结构

```
ESPCLAW/
├── README.md                    # 本文件
├── CLAUDE.md                    # Claude Code 项目配置
├── AGENTS.md                    # Agent 配置
├── 项目进度.md                   # 详细进度追踪
├── 项目经验总结.md               # 踩坑经验汇总
├── 项目问题.md                   # 问题记录与解决方案
├── ESPCLAW0427 修改内容.md       # 0425-0427 修改记录
├── Ref/                         # esp-claw 上游源码（只读参考）
│   └── 项目解析.md               # esp-claw 完整解析文档
├── esp-claw/                    # esp-claw 工作副本（实际修改目录）
│   └── application/basic_demo/  # IDF 项目根目录
├── 14-handheld/                  # 立创实战派原始例程 (IDF v5.4.3)
└── 14-handheld(new)/            # 已适配 IDF v5.5.1 的例程（板级驱动参考）
```

## 软件版本

| 项目 | 版本 |
|------|------|
| **ESP-IDF** | v5.5.1 |
| **esp-claw** | 上游 main 分支 |
| **LLM** | DeepSeek-v4-flash (OpenAI Compatible API) |

## 开发环境

- **IDE**: VS Code + ESP-IDF 插件
- **IDF 路径**: `E:\ESPIDF\551\v5.5.1\esp-idf`
- **工具链路径**: `E:\ESPIDF\551\Tools_551`
- **构建方式**: `build_helper.bat`（通过 cmd.exe 绕过 Git Bash MSYSTEM 检测）
- **GitHub**: https://github.com/GBall5599/-ESP-CLAW
