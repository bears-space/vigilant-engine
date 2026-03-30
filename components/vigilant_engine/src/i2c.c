#include "i2c.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"
#include "sdkconfig.h"

#define I2C_SCL_IO          CONFIG_VE_I2C_SCL_IO
#define I2C_SDA_IO          CONFIG_VE_I2C_SDA_IO
#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         CONFIG_VE_I2C_FREQ_HZ
#define I2C_TIMEOUT_MS      50
#define I2C_TEST_ADDR       0x18

static const char *TAG = "ve_i2c";


static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_i2c_dev = NULL;

static esp_err_t i2c_read_reg8(uint8_t reg, uint8_t *value) // Helper function to read a single register
{
    if (!s_i2c_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_i2c_dev, &reg, 1, value, 1, 100);
}

esp_err_t i2c_add_device(uint16_t address, i2c_master_dev_handle_t *out_dev) // Helper function to add a device to the bus
{
    if (!s_i2c_bus) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!out_dev) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, out_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device 0x%02X: %s",
                 address, esp_err_to_name(err));
    }

    return err;
}

esp_err_t i2c_init(void)
{
    ESP_LOGI(TAG, "Bus initializing... SCL=%d SDA=%d FREQ=%dHz",
             I2C_SCL_IO, I2C_SDA_IO, I2C_FREQ_HZ);

    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT,
        .scl_io_num = I2C_SCL_IO,
        .sda_io_num = I2C_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Bus initialized successfully");
    ESP_LOGI(TAG, "I2C bus scan on SDA=%d SCL=%d", I2C_SDA_IO, I2C_SCL_IO);

    char line[128];
    int found = 0;

    strcpy(line, "    ");
    for (int i = 0; i < 16; i++) {
        char tmp[4];
        snprintf(tmp, sizeof(tmp), "%02X ", i);
        strncat(line, tmp, sizeof(line) - strlen(line) - 1);
    }
    ESP_LOGI(TAG, "%s", line);

    for (int high = 0; high < 8; high++) {
        snprintf(line, sizeof(line), "%02X: ", high << 4);

        for (int low = 0; low < 16; low++) {
            uint8_t addr = (high << 4) | low;
            char cell[4];

            if (addr < 0x03 || addr > 0x77) {
                snprintf(cell, sizeof(cell), "   ");
            } else {
                err = i2c_master_probe(s_i2c_bus, addr, I2C_TIMEOUT_MS);
                if (err == ESP_OK) {
                    snprintf(cell, sizeof(cell), "%02X ", addr);
                    found++;
                } else {
                    snprintf(cell, sizeof(cell), "-- ");
                }
            }

            strncat(line, cell, sizeof(line) - strlen(line) - 1);
        }

        ESP_LOGI(TAG, "%s", line);
    }

    ESP_LOGI(TAG, "Scan complete, found %d device(s)", found);

    err = i2c_add_device(I2C_TEST_ADDR, &s_i2c_dev);
    if (err != ESP_OK) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return err;
    }

    uint8_t value = 0;
    err = i2c_read_reg8(0x0F, &value);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "0x%02X: reg 0x0F = 0x%02X", I2C_TEST_ADDR, value);
    } else {
        ESP_LOGE(TAG, "Failed to read reg 0x0F from 0x%02X: %s",
                I2C_TEST_ADDR, esp_err_to_name(err));
    }

    return ESP_OK;
}

void i2c_deinit(void)
{
    if (s_i2c_dev) {
        i2c_master_bus_rm_device(s_i2c_dev);
        s_i2c_dev = NULL;
    }

    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
}
