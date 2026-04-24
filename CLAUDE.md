# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目简介

将 [esp-claw](https://github.com/espressif/esp-claw)（乐鑫 AI Agent 框架）移植到立创实战派 ESP32-S3 开发板（代号 SZP）。

- **芯片**: ESP32-S3-WROOM-1-N16R8 (16MB Flash + 8MB Octal PSRAM)
- **ESP-IDF**: v5.5.1（路径: `E:\ESPIDF\551\v5.5.1\esp-idf`）
- **工具链**: `E:\ESPIDF\551\Tools_551`
- **LLM 后端**: DeepSeek-v4-flash（通过 OpenAI Compatible API）
- **交互方式**: 网页聊天 `http://<设备IP>/chat`

## 目录结构

```
ESPCLAW/
├── Ref/                    # esp-claw 上游源码（只读参考）
│   ├── application/        # 唯一 IDF 项目入口: application/basic_demo/
│   ├── components/         # 共享组件库
│   └── 项目解析.md         # esp-claw 完整解析文档
├── esp-claw/               # esp-claw 工作副本（实际修改目录）
│   └── application/basic_demo/  ← IDF 项目根目录
├── 14-handheld/            # 立创实战派原始例程 (IDF v5.4.3)
├── 14-handheld(new)/       # 已适配 IDF v5.5.1 的例程（板级驱动参考）
├── 项目经验总结.md          # 移植经验文档
└── 项目进度.md             # 移植进度追踪
```

## 构建命令

### esp-claw 项目（主工作目录）

```bash
cd esp-claw/application/basic_demo
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash monitor
```

板级配置生成（首次或修改板级 YAML 后）:
```bash
idf.py gen-bmgr-config -c ./boards -b esp32_s3_szp
```

### 例程项目 (14-handheld(new))
```bash
cd 14-handheld\(new\)
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash monitor
```

### Windows 构建注意事项

从 Claude Code（Git Bash）中构建时，`idf.py` 会检测到 MSYSTEM 环境变量并拒绝运行。解决方案：使用 `build_helper.bat` 通过 `cmd.exe` 调用：

```bat
set MSYSTEM=
set IDF_PATH=e:\ESPIDF\551\v5.5.1\esp-idf
set IDF_TOOLS_PATH=e:\ESPIDF\551\Tools_551
set IDF_PYTHON_ENV_PATH=E:\ESPIDF\551\Tools_551\python_env\idf5.5_py3.11_env
set PYTHONIOENCODING=utf-8
set PATH=E:\ESPIDF\551\Tools_551\tools\cmake\3.30.2\bin;...;%PATH%
python %IDF_PATH%\tools\idf.py build
```

## esp-claw 架构

### 组件体系 (esp-claw/components/)

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

### SZP 板级设备

| 设备 | 驱动芯片 | 接口 | 板级类型 |
|------|---------|------|---------|
| IO 扩展器 | PCA9557 | I2C 0x19 | custom（setup_device.c 中手动初始化） |
| 音频 DAC | ES8311 | I2C 0x30 + I2S | audio_codec |
| 音频 ADC | ES7210 | I2C 0x82 + I2S | audio_codec |
| LCD | ST7789 | SPI, 320x240 | display_lcd (sub_type: spi) |
| 触摸 | FT5x06 | I2C 0x38 | lcd_touch_i2c |
| 背光 PWM | LEDC | GPIO42 | ledc (peripheral) |

### 数据流

用户消息 → IM 能力(QQ/TG/微信/飞书) 或 网页聊天(/chat) → event_router → 路由规则匹配 → claw_core 提交 → LLM API → 工具执行(claw_cap_call) → 响应 → event_router 出站绑定 → IM 发送

### 网页聊天

`config_http_server.c` 中实现了两个 HTTP 端点：
- `GET /chat` — 返回内嵌 HTML/JS 的聊天页面
- `POST /api/ask` — 接收 JSON `{"message":"..."}` 调用 `claw_core_submit()` + `claw_core_receive_for()` 获取 LLM 响应

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

### gen_bmgr_codes CONFIG 选项

`gen_bmgr_codes` 自动生成的代码引用 `esp_board_manager` 设备/外设子目录的头文件。这些头文件路径只在对应 `CONFIG_ESP_BOARD_*_SUPPORT` 启用时才会加入 INCLUDE_DIRS。必须在 `sdkconfig.defaults` 中显式声明所有需要的 CONFIG 选项，否则 sdkconfig 重新生成后会编译失败。

### cJSON 内存管理

通过 cJSON 解析的字符串（`valuestring`）在 `cJSON_Delete()` 后会失效。如果需要在 cJSON 树释放后继续使用字符串，必须先用 `strdup()` 复制。

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
