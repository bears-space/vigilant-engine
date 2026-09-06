#pragma once

#include "driver/i2c_master.h"

typedef struct {
    esp_err_t create_result, delete_result, add_result, remove_result;
    esp_err_t read_result, write_result;
    esp_err_t probe_results[128];
    unsigned create_calls, delete_calls, add_calls, remove_calls;
    unsigned read_calls, write_calls, probe_calls;
    uint16_t probed_addresses[128];
    i2c_master_bus_config_t bus_config;
    i2c_device_config_t device_config;
    i2c_master_dev_handle_t last_device;
    uint8_t read_data[256];
    uint8_t write_data[256];
    size_t read_size, write_size, buffer_count;
    int timeout_ms;
} fake_i2c_t;

extern fake_i2c_t fake_i2c;
extern const i2c_master_bus_handle_t fake_bus;
extern const i2c_master_dev_handle_t fake_device;
void fake_i2c_reset(void);
