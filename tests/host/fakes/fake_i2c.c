#include "fake_i2c.h"

#include <string.h>

#include "unity.h"

struct i2c_master_bus_t {
    int unused;
};
struct i2c_master_dev_t {
    int unused;
};
static struct i2c_master_bus_t bus_storage;
static struct i2c_master_dev_t device_storage;
const i2c_master_bus_handle_t fake_bus = &bus_storage;
const i2c_master_dev_handle_t fake_device = &device_storage;
fake_i2c_t fake_i2c;

void fake_i2c_reset(void) {
    memset(&fake_i2c, 0, sizeof(fake_i2c));
    for (size_t i = 0; i < 128; ++i) {
        fake_i2c.probe_results[i] = ESP_ERR_NOT_FOUND;
    }
}

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t* config,
                             i2c_master_bus_handle_t* handle) {
    ++fake_i2c.create_calls;
    fake_i2c.bus_config = *config;
    if (fake_i2c.create_result == ESP_OK) *handle = fake_bus;
    return fake_i2c.create_result;
}

esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t handle) {
    TEST_ASSERT_EQUAL_PTR(fake_bus, handle);
    ++fake_i2c.delete_calls;
    return fake_i2c.delete_result;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address,
                           int timeout_ms) {
    TEST_ASSERT_EQUAL_PTR(fake_bus, bus);
    TEST_ASSERT_LESS_THAN_UINT(128, address);
    TEST_ASSERT_LESS_THAN_UINT(128, fake_i2c.probe_calls);
    fake_i2c.probed_addresses[fake_i2c.probe_calls++] = address;
    fake_i2c.timeout_ms = timeout_ms;
    return fake_i2c.probe_results[address];
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                    const i2c_device_config_t* config,
                                    i2c_master_dev_handle_t* handle) {
    TEST_ASSERT_EQUAL_PTR(fake_bus, bus);
    ++fake_i2c.add_calls;
    fake_i2c.device_config = *config;
    if (fake_i2c.add_result == ESP_OK) *handle = fake_device;
    return fake_i2c.add_result;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle) {
    ++fake_i2c.remove_calls;
    fake_i2c.last_device = handle;
    return fake_i2c.remove_result;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t handle,
                                      const uint8_t* write_buffer,
                                      size_t write_size, uint8_t* read_buffer,
                                      size_t read_size, int timeout_ms) {
    TEST_ASSERT_LESS_OR_EQUAL_UINT(sizeof(fake_i2c.write_data), write_size);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(sizeof(fake_i2c.read_data), read_size);
    ++fake_i2c.read_calls;
    fake_i2c.last_device = handle;
    fake_i2c.write_size = write_size;
    fake_i2c.read_size = read_size;
    fake_i2c.timeout_ms = timeout_ms;
    memcpy(fake_i2c.write_data, write_buffer, write_size);
    if (fake_i2c.read_result == ESP_OK) {
        memcpy(read_buffer, fake_i2c.read_data, read_size);
    }
    return fake_i2c.read_result;
}

esp_err_t i2c_master_multi_buffer_transmit(
    i2c_master_dev_handle_t handle,
    i2c_master_transmit_multi_buffer_info_t* buffers, size_t buffer_count,
    int timeout_ms) {
    ++fake_i2c.write_calls;
    fake_i2c.last_device = handle;
    fake_i2c.buffer_count = buffer_count;
    fake_i2c.timeout_ms = timeout_ms;
    fake_i2c.write_size = 0;
    for (size_t i = 0; i < buffer_count; ++i) {
        TEST_ASSERT_LESS_OR_EQUAL_UINT(
            sizeof(fake_i2c.write_data) - fake_i2c.write_size,
            buffers[i].buffer_size);
        if (buffers[i].buffer_size > 0) {
            TEST_ASSERT_NOT_NULL(buffers[i].write_buffer);
            memcpy(fake_i2c.write_data + fake_i2c.write_size,
                   buffers[i].write_buffer, buffers[i].buffer_size);
        }
        fake_i2c.write_size += buffers[i].buffer_size;
    }
    return fake_i2c.write_result;
}
