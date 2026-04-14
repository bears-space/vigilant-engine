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

static const char *TAG = "ve_i2c";


static i2c_master_bus_handle_t s_i2c_bus = NULL;

esp_err_t i2c_init(void)
{
    if (s_i2c_bus) {
        ESP_LOGI(TAG, "I2C bus already initialized");
        return ESP_OK;
    }

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

    return ESP_OK;
}

esp_err_t i2c_add_device(VigilantI2CDevice *device)
{
    if (!s_i2c_bus) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!device) {
        return ESP_ERR_INVALID_ARG;
    }
    if (device->handle) {
        ESP_LOGW(TAG, "I2C device 0x%02X already added", (unsigned int)device->address);
        return ESP_OK;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device->address,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &device->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device 0x%02X: %s",
                 (unsigned int)device->address, esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t i2c_remove_device(VigilantI2CDevice *device)
{
    if (!device || !device->handle) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = i2c_master_bus_rm_device(device->handle);
    if (err == ESP_OK) {
        device->handle = NULL;
    }

    return err;
}

esp_err_t i2c_set_reg8(VigilantI2CDevice *device, uint8_t reg, uint8_t value)
{
    if (!device) {
        ESP_LOGE(TAG, "Cannot write reg 0x%02X: device object is NULL", reg);
        return ESP_ERR_INVALID_ARG;
    }
    if (!device->handle) {
        ESP_LOGE(TAG, "Cannot write reg 0x%02X: device 0x%02X has no handle. Call i2c_add_device() first.",
                 reg, (unsigned int)device->address);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t payload[] = {reg, value};
    return i2c_master_transmit(device->handle, payload, sizeof(payload), 100);
}

esp_err_t i2c_read_reg8(VigilantI2CDevice *device, uint8_t reg, uint8_t *value)
{
    if (!device) {
        ESP_LOGE(TAG, "Cannot read reg 0x%02X: device object is NULL", reg);
        return ESP_ERR_INVALID_ARG;
    }
    if (!device->handle) {
        ESP_LOGE(TAG, "Cannot read reg 0x%02X: device 0x%02X has no handle. Call i2c_add_device() first.",
                 reg, (unsigned int)device->address);
        return ESP_ERR_INVALID_ARG;
    }
    if (!value) {
        ESP_LOGE(TAG, "Cannot read reg 0x%02X: output buffer is NULL", reg);
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit_receive(device->handle, &reg, 1, value, 1, 100);
}

esp_err_t i2c_whoami_check(VigilantI2CDevice *device)
{
    if (!device) {
        ESP_LOGE(TAG, "Cannot run WHOAMI check: device object is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t value = 0;

    esp_err_t err = i2c_read_reg8(device, device->whoami_reg, &value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WHOAMI reg 0x%02X: %s",
                 device->whoami_reg, esp_err_to_name(err));
        return err;
    }

    if (value != device->expected_whoami) {
        ESP_LOGE(TAG, "WHOAMI mismatch: expected 0x%02X but got 0x%02X",
                 device->expected_whoami, value);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "WHOAMI check passed: 0x%02X", value);
    return ESP_OK;
}

void i2c_deinit(void)
{
    if (s_i2c_bus) {
        esp_err_t err = i2c_del_master_bus(s_i2c_bus);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete I2C bus: %s", esp_err_to_name(err));
            return;
        }

        s_i2c_bus = NULL;
    }
}
