/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_board_manager_includes.h"
#include "gen_board_device_custom.h"

static const char *TAG = "SZP_SETUP_DEVICE";

/* PCA9557 register addresses */
#define PCA9557_REG_INPUT    0x00
#define PCA9557_REG_OUTPUT   0x01
#define PCA9557_REG_POLARITY 0x02
#define PCA9557_REG_CONFIG   0x03

/* PCA9557 pin assignments on SZP board */
#define PCA9557_PIN_LCD_CS   0   /* BIT(0) - LCD chip select, active low */
#define PCA9557_PIN_PA_EN    1   /* BIT(1) - power amplifier enable */
#define PCA9557_PIN_DVP_PWDN 2   /* BIT(2) - camera power down */

static i2c_master_dev_handle_t s_pca9557_handle = NULL;

static esp_err_t pca9557_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = {reg, data};
    return i2c_master_transmit(s_pca9557_handle, buf, sizeof(buf), -1);
}

static int io_expander_init(void *config, int cfg_size, void **device_handle)
{
    const dev_custom_io_expander_config_t *cfg = (const dev_custom_io_expander_config_t *)config;
    ESP_LOGI(TAG, "Initializing PCA9557 IO expander, i2c_addr=0x%02x", cfg->i2c_addr);

    /* Get I2C master bus handle from board manager */
    i2c_master_bus_handle_t bus_handle = NULL;
    esp_err_t ret = esp_board_periph_get_handle(cfg->peripheral_name, (void **)&bus_handle);
    if (ret != ESP_OK || bus_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get I2C bus handle for peripheral: %s", cfg->peripheral_name);
        return ESP_FAIL;
    }

    /* Add PCA9557 device on the I2C bus */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = cfg->i2c_addr,
        .scl_speed_hz = 100000,
    };
    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_pca9557_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add PCA9557 device: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure P0/P1/P2 as output (config register bits: 0=output, 1=input)
     * Bits 0,1,2 = 0 (output), bits 3-7 = 1 (input) = 0xF8 */
    ret = pca9557_write_reg(PCA9557_REG_CONFIG, 0xF8);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure PCA9557 pins");
        return ret;
    }

    /* Set initial outputs:
     * LCD_CS (P0) = 0  (asserted, enable LCD)
     * PA_EN  (P1) = 1  (enable power amplifier)
     * DVP_PWDN (P2) = 0 (camera normal operation)
     * Output value = BIT(1) = 0x02 */
    ret = pca9557_write_reg(PCA9557_REG_OUTPUT, 0x02);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set PCA9557 outputs");
        return ret;
    }

    ESP_LOGI(TAG, "PCA9557 initialized: LCD_CS=LOW, PA_EN=HIGH, DVP_PWDN=LOW");

    *device_handle = s_pca9557_handle;
    return ESP_OK;
}

static int io_expander_deinit(void *device_handle)
{
    if (s_pca9557_handle) {
        i2c_master_bus_rm_device(s_pca9557_handle);
        s_pca9557_handle = NULL;
    }
    return ESP_OK;
}

CUSTOM_DEVICE_IMPLEMENT(io_expander, io_expander_init, io_expander_deinit);

/* ═══════════════════════════════════════════════════
   QMI8658 IMU - ported from 14-handheld(new)
   ═══════════════════════════════════════════════════ */

/* QMI8658 register addresses */
enum qmi8658_reg {
    QMI8658_WHO_AM_I = 0,
    QMI8658_CTRL1, QMI8658_CTRL2, QMI8658_CTRL3,
    QMI8658_CTRL4, QMI8658_CTRL5, QMI8658_CTRL6,
    QMI8658_CTRL7, QMI8658_CTRL8, QMI8658_CTRL9,
    QMI8658_CATL1_L, QMI8658_CATL1_H,
    QMI8658_CATL2_L, QMI8658_CATL2_H,
    QMI8658_CATL3_L, QMI8658_CATL3_H,
    QMI8658_CATL4_L, QMI8658_CATL4_H,
    QMI8658_STATUS0 = 46,
    QMI8658_STATUS1,
    QMI8658_AX_L = 53,
    QMI8658_RESET = 96,
};

static i2c_master_dev_handle_t s_qmi8658_handle = NULL;

static esp_err_t qmi8658_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_qmi8658_handle, buf, sizeof(buf), -1);
}

static esp_err_t qmi8658_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_qmi8658_handle, &reg, 1, data, len, -1);
}

static int imu_init(void *config, int cfg_size, void **device_handle)
{
    const dev_custom_imu_config_t *cfg = (const dev_custom_imu_config_t *)config;
    ESP_LOGI(TAG, "Initializing QMI8658 IMU, i2c_addr=0x%02x", cfg->i2c_addr);

    i2c_master_bus_handle_t bus_handle = NULL;
    esp_err_t ret = esp_board_periph_get_handle(cfg->peripheral_name, (void **)&bus_handle);
    if (ret != ESP_OK || bus_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get I2C bus for IMU");
        return ESP_FAIL;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = cfg->i2c_addr,
        .scl_speed_hz = 100000,
    };
    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_qmi8658_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add QMI8658 device: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Verify WHO_AM_I (expected 0x05) */
    uint8_t id = 0;
    int retry = 0;
    qmi8658_read_reg(QMI8658_WHO_AM_I, &id, 1);
    while (id != 0x05 && retry < 3) {
        vTaskDelay(pdMS_TO_TICKS(100));
        qmi8658_read_reg(QMI8658_WHO_AM_I, &id, 1);
        retry++;
    }
    if (id != 0x05) {
        ESP_LOGE(TAG, "QMI8658 WHO_AM_I mismatch: 0x%02x (expected 0x05)", id);
        return ESP_FAIL;
    }

    /* Soft reset */
    qmi8658_write_reg(QMI8658_RESET, 0xb0);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Configure motion detection */
    qmi8658_write_reg(QMI8658_CATL1_L, 1);
    qmi8658_write_reg(QMI8658_CATL1_H, 1);
    qmi8658_write_reg(QMI8658_CATL2_L, 1);
    qmi8658_write_reg(QMI8658_CATL2_H, 1);
    qmi8658_write_reg(QMI8658_CATL3_L, 1);
    qmi8658_write_reg(QMI8658_CATL3_H, 1);
    qmi8658_write_reg(QMI8658_CATL4_L, 0x77);
    qmi8658_write_reg(QMI8658_CATL4_H, 0x01);
    qmi8658_write_reg(QMI8658_CTRL9, 0x0E);

    qmi8658_write_reg(QMI8658_CATL1_L, 1);
    qmi8658_write_reg(QMI8658_CATL1_H, 1);
    qmi8658_write_reg(QMI8658_CATL2_L, 0xE8);
    qmi8658_write_reg(QMI8658_CATL2_H, 0x03);
    qmi8658_write_reg(QMI8658_CATL3_L, 0xE8);
    qmi8658_write_reg(QMI8658_CATL3_H, 0x03);
    qmi8658_write_reg(QMI8658_CATL4_H, 0x02);
    qmi8658_write_reg(QMI8658_CTRL9, 0x0E);

    /* Enable ACC + GYR */
    qmi8658_write_reg(QMI8658_CTRL1, 0x40);
    qmi8658_write_reg(QMI8658_CTRL7, 0x03);
    qmi8658_write_reg(QMI8658_CTRL2, 0x95); /* ACC: 4g, 250Hz */
    qmi8658_write_reg(QMI8658_CTRL3, 0xd5); /* GYR: 512dps, 250Hz */
    qmi8658_write_reg(QMI8658_CTRL8, 0x0E);

    ESP_LOGI(TAG, "QMI8658 initialized: WHO_AM_I=0x%02x", id);
    *device_handle = s_qmi8658_handle;
    return ESP_OK;
}

static int imu_deinit(void *device_handle)
{
    if (s_qmi8658_handle) {
        qmi8658_write_reg(QMI8658_CTRL1, 0x01);
        i2c_master_bus_rm_device(s_qmi8658_handle);
        s_qmi8658_handle = NULL;
    }
    return ESP_OK;
}

CUSTOM_DEVICE_IMPLEMENT(imu, imu_init, imu_deinit);

esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                     const esp_lcd_panel_dev_config_t *panel_dev_config,
                                     esp_lcd_panel_handle_t *ret_panel)
{
    esp_lcd_panel_dev_config_t panel_dev_cfg = {0};
    memcpy(&panel_dev_cfg, panel_dev_config, sizeof(esp_lcd_panel_dev_config_t));
    esp_err_t ret = esp_lcd_new_panel_st7789(io, &panel_dev_cfg, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ST7789 panel: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t lcd_touch_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                     const esp_lcd_touch_config_t *touch_dev_config,
                                     esp_lcd_touch_handle_t *ret_touch)
{
    esp_err_t ret = esp_lcd_touch_new_i2c_ft5x06(io, touch_dev_config, ret_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create FT5x06 touch driver: %s", esp_err_to_name(ret));
    }
    return ret;
}
