# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目简介

将 [esp-claw](https://github.com/espressif/esp-claw)（乐鑫 AI Agent 框架）移植到立创实战派 ESP32-S3 开发板（代号 SZP）。

- **芯片**: ESP32-S3-WROOM-1-N16R8 (16MB Flash + 8MB Octal PSRAM)
- **ESP-IDF**: v5.5.1（路径: `E:\ESPIDF\551\v5.5.1\esp-idf`）
- **工具链**: `E:\ESPIDF\551\Tools_551`

## 目录结构

```
ESPCLAW/
├── Ref/                    # esp-claw 上游源码（只读参考）
│   ├── application/        # 唯一 IDF 项目入口: application/basic_demo/
│   ├── components/         # 共享组件库
│   └── 项目解析.md         # esp-claw 完整解析文档
├── 14-handheld/            # 立创实战派原始例程 (IDF v5.4.3)
├── 14-handheld(new)/       # 已适配 IDF v5.5.1 的例程（板级驱动参考）
├── 项目经验总结.md          # 移植经验文档
└── 项目进度.md             # 移植进度追踪
```

## 构建命令

### 例程项目 (14-handheld(new))
```bash
cd 14-handheld\(new\)
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash monitor
```

### esp-claw 项目 (Ref/)
```bash
cd Ref/application/basic_demo
idf.py set-target esp32s3
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults" build
```

板级配置生成（移植阶段使用）:
```bash
idf.py gen-bmgr-config -c ./boards -b <board_name>
```

## esp-claw 架构

### 组件体系 (Ref/components/)

**核心模块** (`claw_modules/`):
- `claw_core` — LLM Agent 引擎，管理请求队列、工具调用循环（最多 20 次迭代）、上下文提供者链
- `claw_cap` — 能力注册与调度系统，能力分为 CALLABLE/EVENT_SOURCE/HYBRID 三类
- `claw_event_router` — 事件驱动消息总线，支持 JSON 路由规则
- `claw_memory` — 持久化记忆系统（FULL 模式：长期+档案+会话；LIGHTWEIGHT 模式：仅档案）
- `claw_skill` — 技能管理（Markdown 文档 + 会话级激活状态）

**能力插件** (`claw_capabilities/`): QQ/微信/飞书/Telegram IM 集成、Lua 脚本执行、MCP Server/Client、文件操作、定时调度、Web 搜索等。每个能力通过 `register_group()` 注册。

**Lua 模块** (`lua_modules/`): 硬件绑定模块（GPIO/I2C/SPI/UART/ADC/摄像头/显示屏/音频/触摸等），由 `cap_lua` 调用，依赖 `esp_board_manager` 获取设备句柄。

### 板级管理系统

esp-claw 使用 `esp_board_manager` 组件通过 YAML 文件定义硬件:

| 文件 | 作用 |
|------|------|
| `board_info.yaml` | 板子身份（芯片、名称、厂商） |
| `board_peripherals.yaml` | 总线定义（I2C/SPI/I2S 引脚和参数） |
| `board_devices.yaml` | 设备列表（类型、驱动芯片、依赖、关联总线） |
| `sdkconfig.defaults.board` | 板级 Kconfig 配置 |
| `setup_device.c` | 自定义设备工厂函数（LCD 面板、IO 扩展器等） |

现有参考板: `m5stack_cores3`（最接近 SZP，有 I2C 音频编解码器 + SPI LCD + I2C 触摸）。

### 数据流

用户消息 → IM 能力(QQ/TG/微信/飞书) → event_router → 路由规则匹配 → claw_core 提交 → LLM API → 工具执行(claw_cap_call) → 响应 → event_router 出站绑定 → IM 发送

## 关键注意事项

### IDF v5.5.1 I2C 迁移

本项目使用新 I2C 驱动 (`driver/i2c_master.h`)。旧 API (`driver/i2c.h`) 与新 API 不能在同一端口混用。必须在 `sdkconfig.defaults` 中设置:
```
CONFIG_I2C_SKIP_LEGACY_CONFLICT_CHECK=y
```
因为 IDF 的 `esp_lcd` 组件总是同时编译 v1(旧API) 和 v2(新API) 驱动。

调用 `esp_lcd_new_panel_io_i2c()` 时不要将 `i2c_master_bus_handle_t` 强转为 `esp_lcd_i2c_bus_handle_t`（= `uint32_t`），否则 `_Generic` 宏会匹配到旧 API 分支。

### sdkconfig.defaults 编码

所有 sdkconfig.defaults 注释使用英文。IDF 的 Python 工具链在中文 Windows 上用 GBK 编码读取文件，UTF-8 中文字符会导致构建失败。

### SZP 硬件引脚 (BSP 参考: 14-handheld(new)/main/esp32_s3_szp.h)

| 外设 | 接口 | 关键引脚 |
|------|------|---------|
| I2C 总线 | I2C #0, 100kHz | SDA=GPIO1, SCL=GPIO2 |
| LCD (ST7789) | SPI3 | MOSI=40, CLK=41, DC=39, BL=42 |
| 触摸 (FT5x06) | I2C | 共享 I2C 总线 |
| 音频 DAC (ES8311) | I2C+I2S | MCLK=38, BCK=14, WS=13, DOUT=45 |
| 音频 ADC (ES7210) | I2C+I2S | SDIN=12 |
| 摄像头 (GC0308) | DVP | XCLK=5 |
| IO 扩展 (PCA9557) | I2C 0x19 | 控制 LCD_CS/PA_EN/DVP_PWDN |
| SD 卡 | SDMMC 1-bit | CMD=48, CLK=47, DAT0=21 |
| IMU (QMI8658) | I2C 0x6A | 共享 I2C 总线 |
