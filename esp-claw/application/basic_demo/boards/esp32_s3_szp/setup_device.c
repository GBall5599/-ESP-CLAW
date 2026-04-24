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
