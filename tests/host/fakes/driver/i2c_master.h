#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* Minimal source-compatible subset used by VE, checked against ESP-IDF v6.0.
 * This is a test double, not an implementation of the ESP-IDF driver or ABI. */
typedef struct i2c_master_bus_t* i2c_master_bus_handle_t;
typedef struct i2c_master_dev_t* i2c_master_dev_handle_t;

#define I2C_NUM_0 0
#define I2C_CLK_SRC_DEFAULT 0
#define I2C_ADDR_BIT_LEN_7 0

typedef struct {
    int clk_source;
    int i2c_port;
    int scl_io_num;
    int sda_io_num;
    uint8_t glitch_ignore_cnt;
    struct {
        uint32_t enable_internal_pullup : 1;
    } flags;
} i2c_master_bus_config_t;

typedef struct {
    int dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
} i2c_device_config_t;

typedef struct {
    const uint8_t* write_buffer;
    size_t buffer_size;
} i2c_master_transmit_multi_buffer_info_t;

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t* config,
                             i2c_master_bus_handle_t* handle);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t handle);
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address,
                           int timeout_ms);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                    const i2c_device_config_t* config,
                                    i2c_master_dev_handle_t* handle);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t handle,
                                      const uint8_t* write_buffer,
                                      size_t write_size, uint8_t* read_buffer,
                                      size_t read_size, int timeout_ms);
esp_err_t i2c_master_multi_buffer_transmit(
    i2c_master_dev_handle_t handle,
    i2c_master_transmit_multi_buffer_info_t* buffers, size_t buffer_count,
    int timeout_ms);
