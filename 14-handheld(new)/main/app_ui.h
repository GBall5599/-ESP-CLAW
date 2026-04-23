/**
 * @file app_ui.h
 * @brief 立创实战派 ESP32-S3 手持设备 - LVGL 用户界面
 *
 * 提供以下 UI 功能：
 * - 开机画面 (Logo + 进度条)
 * - 主界面 (6 个功能图标：WiFi设置/蓝牙/音乐/摄像头/SD卡/姿态)
 * - WiFi 扫描连接界面
 * - BLE HID 设备界面
 * - MP3 音乐播放器界面
 * - 摄像头预览界面
 * - 姿态传感器界面
 * - SD 卡文件浏览界面
 */
#pragma once


/*********************** 开机界面 ****************************/
void lv_gui_start(void);   // 显示开机 Logo 和进度条


/*********************** 主界面 ****************************/
void lv_main_page(void);   // 显示主界面（6 个功能入口图标）


/*********************** 音乐播放器 ****************************/
void mp3_player_init(void);  // 初始化 MP3 播放器（挂载 SPIFFS、创建播放器实例）
void music_ui(void);         // 显示音乐播放器界面

// AI 语音助手相关
void ai_gui_in(void);        // 进入语音助手界面
void ai_gui_out(void);       // 退出语音助手界面

// 音乐控制函数
void ai_play(void);          // 播放
void ai_pause(void);         // 暂停
void ai_resume(void);        // 恢复播放
void ai_prev_music(void);    // 上一首
void ai_next_music(void);    // 下一首
void ai_volume_up(void);     // 音量+
void ai_volume_down(void);   // 音量-


// 主界面图标激活效果（按下动画）
void ai_open_icon1(void);    // WiFi 设置图标
void ai_open_icon2(void);    // 蓝牙图标
void ai_open_icon3(void);    // 音乐图标
void ai_open_icon4(void);    // 摄像头图标
void ai_open_icon5(void);    // SD 卡图标
void ai_open_icon6(void);    // 姿态传感器图标
void ai_tuichu(void);        // 退出当前功能，返回主界面
