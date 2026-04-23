/**
 * @file main.c
 * @brief 立创实战派 ESP32-S3 手持设备 - 主入口
 *
 * 程序入口，负责：
 * 1. 初始化 NVS、I2C、IO 扩展器、LCD、LVGL、SPIFFS、音频编解码器
 * 2. 显示开机画面并播放开机音乐
 * 3. 音乐播放完毕后进入主界面
 * 4. 后台循环打印内存使用情况
 */

#include <stdio.h>
#include "esp32_s3_szp.h"
#include "app_ui.h"
#include "nvs_flash.h"
#include <esp_system.h>

// 全局事件组：用于任务间同步（如等待开机音乐播放完成）
EventGroupHandle_t my_event_group;

static const char *TAG = "MAIN";

/**
 * @brief 打印 DRAM 和 PSRAM 的使用情况
 *
 * 包括总量、已用、可用、最大连续块和使用百分比
 */
void displayMemoryUsage() {
    size_t totalDRAM = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t freeDRAM = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t usedDRAM = totalDRAM - freeDRAM;

    size_t totalPSRAM = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t freePSRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t usedPSRAM = totalPSRAM - freePSRAM;

    size_t DRAM_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t PSRAM_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    float dramUsagePercentage = (float)usedDRAM / totalDRAM * 100;
    float psramUsagePercentage = (float)usedPSRAM / totalPSRAM * 100;

    ESP_LOGI(TAG, "DRAM Total: %zu bytes, Used: %zu bytes, Free: %zu bytes,  DRAM_Largest_block: %zu bytes", totalDRAM, usedDRAM, freeDRAM, DRAM_largest_block);
    ESP_LOGI(TAG, "DRAM Used: %.2f%%", dramUsagePercentage);
    ESP_LOGI(TAG, "PSRAM Total: %zu bytes, Used: %zu bytes, Free: %zu bytes, PSRAM_Largest_block: %zu bytes", totalPSRAM, usedPSRAM, freePSRAM, PSRAM_largest_block);
    ESP_LOGI(TAG, "PSRAM Used: %.2f%%", psramUsagePercentage);
}

/**
 * @brief 主界面任务
 *
 * 等待开机音乐播放完成后，进入 LVGL 主界面
 */
static void main_page_task(void *pvParameters)
{
    // 阻塞等待开机音乐完成的事件标志
    xEventGroupWaitBits(my_event_group, START_MUSIC_COMPLETED, pdFALSE, pdFALSE, portMAX_DELAY);
    // 进入主界面（WiFi设置、蓝牙、音乐、摄像头等）
    lv_main_page();

    vTaskDelete(NULL);
}

/**
 * @brief 程序入口
 *
 * 初始化顺序：
 * 1. NVS (非易失性存储)
 * 2. I2C 总线 (共享总线：IMU/codec/触摸/摄像头)
 * 3. PCA9557 IO 扩展器 (控制 LCD_CS/PA_EN/DVP_PWDN)
 * 4. LCD + LVGL (显示和触摸)
 * 5. SPIFFS (存储 MP3 等资源文件)
 * 6. 音频编解码器 (ES8311 播放 + ES7210 录音)
 */
void app_main(void)
{
    /* 初始化 NVS（WiFi、BLE 等模块依赖 NVS 存储配置） */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    /* 板级初始化 */
    bsp_i2c_init();       // I2C 总线初始化 (SDA=GPIO1, SCL=GPIO2)
    pca9557_init();       // PCA9557 IO 扩展器初始化 (启用 LCD 和功放)
    bsp_lvgl_start();     // LCD (ST7789) + 触摸 (FT5x06) + LVGL 初始化
    bsp_spiffs_mount();   // 挂载 SPIFFS 分区 (MP3 文件存储)
    bsp_codec_init();     // 音频编解码器初始化 (ES8311 DAC + ES7210 ADC)

    /* 显示开机画面 */
    lv_gui_start();

    /* 创建事件组用于任务同步 */
    my_event_group = xEventGroupCreate();

    /* 启动任务：开机音乐(Core1) + 主界面(Core0) */
    xTaskCreatePinnedToCore(power_music_task, "power_music_task", 4*1024, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(main_page_task, "main_page_task", 4*1024, NULL, 5, NULL, 0);

    /* 后台循环：每 5 秒打印一次内存使用情况 */
    while (true) {
        displayMemoryUsage();
        vTaskDelay(pdMS_TO_TICKS(5 * 1000));
    }
}
