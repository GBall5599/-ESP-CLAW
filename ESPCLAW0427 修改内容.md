# ESPCLAW0425-0427 修改内容

本文档记录 2026 年 4 月 25 日至 27 日的所有修改。

---

## 一、04-25 Bug 修复

### 1. scheduler 初始化改为非致命（修复重启循环）
**文件**: `main/app_claw.c`
**修改**: `ESP_RETURN_ON_ERROR` → `ESP_LOGW` 非致命处理

### 2. cap_time 任务栈修复
**文件**: `components/claw_capabilities/cap_time/src/cap_time.c`
**修改**: 栈策略 `PREFER_PSRAM` → `INTERNAL_ONLY`，栈大小 4096 → 8192

### 3. LCD CS 跳变修复（解决 LCD 不亮）
**文件**: `main/app_expression_emote.c`
**修改**: board_manager 初始化后手动 PCA9557 CS HIGH→LOW + LCD 重新初始化

---

## 二、04-26 Display Arbiter + Emote/Lua 共享

### 4. Display Arbiter 组件
**文件**: `components/display_arbiter/display_arbiter.c` + `include/display_arbiter.h`
**功能**: 引用计数管理 LCD 所有权（EMOTE/LUA），所有权变更回调

### 5. Lua display 模块集成 arbiter
**文件**: `components/lua_modules/lua_module_display/src/lua_module_display.c`
**功能**: `display.present()` 自动 acquire → paint → release

### 6. Emote 所有权切换
**文件**: `main/app_expression_emote.c`
**功能**:
- Lua 接管时 emote_deinit + NOP 回调替换 + 50ms 等待
- Lua 释放后 reinit task 自动重建 emote
- WiFi 状态保存/恢复
- `display.set_resume_delay(seconds)` API

### 7. 全局禁用 PSRAM 栈
**文件**: `sdkconfig.defaults`
**修改**: `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=n`

---

## 三、04-27 SPI DMA 内存优化 + 构建修复

### 8. SPI DMA 内存优化（核心修复）
**文件**: `boards/esp32_s3_szp/board_peripherals.yaml`
**修改**: `max_transfer_sz: 12800` → `max_transfer_sz: 3200`
**效果**: gen-bmgr-config 自动将 `trans_queue_depth` 从 10 降到 2
**根因**: PSRAM color buffer → DMA copy 峰值 128KB > 96KB DMA 池

### 9. Emote reinit task 自删除修复
**文件**: `main/app_expression_emote.c`
**修改**: `app_emote_cleanup()` 不再删除 `s_emote_reinit_task`
**根因**: cleanup 从 reinit task 内部调用时，自删除损坏 FreeRTOS 状态

### 10. SPI ISR NOP 回调
**文件**: `main/app_expression_emote.c`
**修改**: Lua 接管时将 SPI 回调替换为 no-op，防止 use-after-free

### 11. 构建：esp_board_manager 依赖修复
**文件**: `managed_components/espressif__esp_board_manager/CMakeLists.txt`
**修改**: REQUIRES 添加 `espressif__esp_codec_dev`、`espressif__esp_lcd_touch`、`espressif__esp_video`

### 12. 构建：gen_bmgr_codes include paths
**文件**: `components/gen_bmgr_codes/CMakeLists.txt`
**修改**: 添加所有设备子目录到 INCLUDE_DIRS，REQUIRES 改用全名

### 13. 构建：LEDC_CTRL CONFIG
**文件**: `sdkconfig.defaults`
**修改**: 添加 `CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT=y`

### 14. 构建：Flash 大小保护
**文件**: `sdkconfig.defaults`
**修改**: 添加 `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`（防止 set-target 重置）

---

## 四、修改文件清单

```
esp-claw/application/basic_demo/
├── main/
│   ├── app_claw.c                          # scheduler 非致命
│   └── app_expression_emote.c              # CS 跳变 + arbiter + reinit + DMA
├── boards/esp32_s3_szp/
│   └── board_peripherals.yaml              # max_transfer_sz 3200
├── sdkconfig.defaults                       # PSRAM/LEDC_CTRL/FLASHSIZE
├── components/
│   ├── display_arbiter/                     # 新组件
│   │   ├── display_arbiter.c
│   │   └── include/display_arbiter.h
│   └── gen_bmgr_codes/
│       └── CMakeLists.txt                   # include paths + REQUIRES
├── managed_components/
│   └── espressif__esp_board_manager/
│       └── CMakeLists.txt                   # REQUIRES 添加外部组件
└── components/lua_modules/lua_module_display/
    └── src/lua_module_display.c             # arbiter 集成

components/claw_capabilities/cap_time/
└── src/cap_time.c                           # 栈策略修复
```

---

## 五、待烧录验证

固件已于 04-27 编译成功（build/basic_demo.bin），COM4 端口不可用，待设备连接后烧录。

验证清单：
- [ ] SPI DMA 优化后 display 模块正常工作
- [ ] Emote → Lua → Emote 切换无崩溃
- [ ] `display.set_resume_delay(seconds)` 持续时间控制
- [ ] 完整流程：Hello World → emote 恢复 → 图片 → emote 恢复
