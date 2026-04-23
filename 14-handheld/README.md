# ESP32-S3 手持设备工程

## 工程简介

本工程实现了ESP32-S3手持设备的基本功能，集成了显示屏、按键、传感器等模块，提供完整的手持设备解决方案。适用于游戏机、控制器、检测仪等手持应用场景。

## 功能特性

- ✅ LCD显示屏驱动
- ✅ 多按键输入支持
- ✅ 电池电量监测
- ✅ 震动反馈
- ✅ 音频提示
- ✅ 低功耗管理
- ✅ 用户界面框架

## 硬件连接

### ESP32-S3引脚配置

| 功能 | 引脚 | 配置 | 说明 |
|------|------|------|------|
| LCD CS | GPIO5 | SPI片选 | LCD片选信号 |
| LCD SCK | GPIO6 | SPI时钟 | LCD时钟线 |
| LCD MOSI | GPIO7 | SPI数据 | LCD数据输出 |
| LCD DC | GPIO8 | 数据/命令 | 数据/命令选择 |
| LCD RST | GPIO9 | 复位 | LCD复位信号 |
| LCD BL | GPIO10 | 背光 | 背光控制 |
| KEYS | GPIO0-4 | 输入 | 多按键输入 |
| BAT_ADC | ADC1_CH0 | ADC | 电池电压检测 |
| MOTOR | GPIO11 | PWM | 震动马达控制 |
| BUZZER | GPIO12 | PWM | 蜂鸣器控制 |

## 技术规格

### 显示参数

- **分辨率**: 320x240像素
- **色彩深度**: 16位色（RGB565）
- **刷新率**: 60Hz
- **背光**: PWM调光

### 输入参数

- **按键数量**: 5个（可扩展）
- **按键类型**: 机械按键/触摸按键
- **扫描频率**: 100Hz
- **去抖时间**: 20ms

### 电源管理

- **电池类型**: 锂电池
- **电压范围**: 3.0V - 4.2V
- **功耗**: 待机<1mA，工作<100mA
- **充电**: 支持USB充电

## 软件架构

### 核心组件

1. **显示驱动**
   - LCD驱动接口
   - 图形渲染引擎
   - UI框架

2. **输入处理**
   - 按键扫描
   - 事件处理
   - 多点触控（如支持）

3. **电源管理**
   - 电池电量检测
   - 低功耗模式
   - 电源状态监控

4. **反馈系统**
   - 震动控制
   - 音频提示
   - LED指示

### 主要函数

```c
// 系统初始化
esp_err_t handheld_init(void);

// 显示功能
void lcd_display_init(void);
void ui_update_screen(void);

// 输入处理
void keypad_scan_task(void *arg);
void input_event_handler(key_event_t event);

// 电源管理
uint8_t get_battery_level(void);
void enter_sleep_mode(void);

// 反馈功能
void vibrate_set(uint8_t level);
void buzzer_beep(uint16_t frequency, uint16_t duration);
```

## 编译和烧录

### 环境要求

- ESP-IDF 5.0+
- ESP32-S3开发板
- LCD显示屏
- 按键模块
- 电池管理电路

### 编译步骤

```bash
cd 14-handheld
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

### 配置选项

在`sdkconfig`中可以配置：
- 显示参数
- 按键映射
- 电源管理策略
- UI主题

## 使用说明

### 运行效果

程序启动后，手持设备显示主界面，支持按键交互：
- 显示电池电量
- 响应按键操作
- 提供震动和音频反馈
- 支持低功耗待机

### 操作步骤

1. **硬件组装**
   - 连接LCD和按键
   - 安装电池管理电路
   - 连接反馈器件

2. **程序运行**
   - 烧录固件到ESP32-S3
   - 测试各项功能

3. **功能测试**
   - 测试按键响应
   - 验证显示效果
   - 检查电池监测

### 用户界面

```c
// 主界面布局
typedef struct {
    uint8_t battery_level;    // 电池电量
    uint8_t signal_strength;  // 信号强度
    char status_text[32];     // 状态文本
    menu_item_t *menu_items;  // 菜单项
} main_ui_t;
```

## 移植指南

### 功能扩展

1. **游戏功能**
2. **传感器集成**
3. **无线通信**
4. **数据存储**

## 故障排除

### 常见问题

1. **显示异常**
   - 检查SPI连接
   - 验证电源供电
   - 调整时序参数

2. **按键无响应**
   - 检查按键连接
   - 验证上拉电阻
   - 调整扫描频率

## 版本信息

- **ESP-IDF版本**: 5.0+
- **硬件平台**: ESP32-S3
- **最后更新**: 2025年12月6日

## 许可证

本项目采用MIT许可证，详见LICENSE文件。