#include <string.h>

#include "fake_i2c.h"
#include "i2c.h"
#include "sdkconfig.h"
#include "unity.h"

static VigilantI2CDevice device;

void setUp(void) {
    fake_i2c_reset();
    i2c_deinit();
    fake_i2c_reset();
    device = (VigilantI2CDevice){
        .address = 0x42, .whoami_reg = 0x0F, .expected_whoami = 0x44};
}

void tearDown(void) {
    /* Recover state even when a test injects a failed bus deletion. */
    fake_i2c.delete_result = ESP_OK;
    i2c_deinit();
}

static void test_requires_initialized_bus(void) {
    uint8_t addresses[16];
    size_t count = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2c_add_device(&device));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      i2c_get_detected_devices(addresses, 16, &count));
    TEST_ASSERT_EQUAL_UINT(0, fake_i2c.add_calls);
}

static void test_initialization_configures_bus_and_scans_valid_addresses(void) {
    TEST_ASSERT_EQUAL(ESP_OK, i2c_init());
    TEST_ASSERT_EQUAL_UINT(1, fake_i2c.create_calls);
    TEST_ASSERT_EQUAL(CONFIG_VE_I2C_SCL_IO, fake_i2c.bus_config.scl_io_num);
    TEST_ASSERT_EQUAL(CONFIG_VE_I2C_SDA_IO, fake_i2c.bus_config.sda_io_num);
    TEST_ASSERT_EQUAL(I2C_NUM_0, fake_i2c.bus_config.i2c_port);
    TEST_ASSERT_EQUAL(I2C_CLK_SRC_DEFAULT, fake_i2c.bus_config.clk_source);
    TEST_ASSERT_EQUAL_UINT(7, fake_i2c.bus_config.glitch_ignore_cnt);
    TEST_ASSERT_TRUE(fake_i2c.bus_config.flags.enable_internal_pullup);
    TEST_ASSERT_EQUAL_UINT(117, fake_i2c.probe_calls);
    for (unsigned i = 0; i < 117; ++i) {
        TEST_ASSERT_EQUAL_UINT(i + 3, fake_i2c.probed_addresses[i]);
    }
    TEST_ASSERT_EQUAL_INT(100, fake_i2c.timeout_ms);
}

static void test_initialization_is_idempotent(void) {
    TEST_ASSERT_EQUAL(ESP_OK, i2c_init());
    TEST_ASSERT_EQUAL(ESP_OK, i2c_init());
    TEST_ASSERT_EQUAL_UINT(1, fake_i2c.create_calls);
    TEST_ASSERT_EQUAL_UINT(117, fake_i2c.probe_calls);
}

static void test_failed_initialization_can_be_retried(void) {
    fake_i2c.create_result = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, i2c_init());
    TEST_ASSERT_EQUAL_UINT(0, fake_i2c.probe_calls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2c_add_device(&device));
    fake_i2c.create_result = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, i2c_init());
    TEST_ASSERT_EQUAL_UINT(2, fake_i2c.create_calls);
}

static void test_scan_caches_only_acknowledged_devices(void) {
    fake_i2c.probe_results[0x03] = ESP_OK;
    fake_i2c.probe_results[0x42] = ESP_OK;
    fake_i2c.probe_results[0x50] = ESP_ERR_TIMEOUT;
    fake_i2c.probe_results[0x77] = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, i2c_init());
    uint8_t addresses[16] = {0};
    const uint8_t expected[] = {0x03, 0x42, 0x77};
    size_t count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, i2c_get_detected_devices(addresses, 16, &count));
    TEST_ASSERT_EQUAL_UINT(3, count);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, addresses, 3);
    TEST_ASSERT_EQUAL_UINT(117, fake_i2c.probe_calls);
}

static void test_scan_truncates_cache_and_respects_output_capacity(void) {
    for (unsigned i = 3; i <= 0x77; ++i) fake_i2c.probe_results[i] = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, i2c_init());
    uint8_t addresses[17];
    memset(addresses, 0xA5, sizeof(addresses));
    size_t count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, i2c_get_detected_devices(addresses, 17, &count));
    TEST_ASSERT_EQUAL_UINT(16, count);
    for (unsigned i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT(i + 3, addresses[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0xA5, addresses[16]);
    memset(addresses, 0xA5, sizeof(addresses));
    TEST_ASSERT_EQUAL(ESP_OK, i2c_get_detected_devices(addresses, 2, &count));
    TEST_ASSERT_EQUAL_UINT(2, count);
    TEST_ASSERT_EQUAL_HEX8(0xA5, addresses[2]);
    TEST_ASSERT_EQUAL(ESP_OK, i2c_get_detected_devices(addresses, 0, &count));
    TEST_ASSERT_EQUAL_UINT(0, count);
    TEST_ASSERT_EQUAL_HEX8(3, addresses[0]);
}

static void test_scan_rejects_null_count(void) {
    TEST_ASSERT_EQUAL(ESP_OK, i2c_init());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      i2c_get_detected_devices(NULL, 0, NULL));
}

static void test_add_device_configures_address_and_speed_once(void) {
    TEST_ASSERT_EQUAL(ESP_OK, i2c_init());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2c_add_device(NULL));
    TEST_ASSERT_EQUAL(ESP_OK, i2c_add_device(&device));
    TEST_ASSERT_EQUAL_PTR(fake_device, device.handle);
    TEST_ASSERT_EQUAL_HEX16(0x42, fake_i2c.device_config.device_address);
    TEST_ASSERT_EQUAL(I2C_ADDR_BIT_LEN_7,
                      fake_i2c.device_config.dev_addr_length);
    TEST_ASSERT_EQUAL_UINT(CONFIG_VE_I2C_FREQ_HZ,
                           fake_i2c.device_config.scl_speed_hz);
    TEST_ASSERT_EQUAL(ESP_OK, i2c_add_device(&device));
    TEST_ASSERT_EQUAL_UINT(1, fake_i2c.add_calls);
}

static void test_add_device_propagates_failure_and_allows_retry(void) {
    TEST_ASSERT_EQUAL(ESP_OK, i2c_init());
    fake_i2c.add_result = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, i2c_add_device(&device));
    TEST_ASSERT_NULL(device.handle);
    fake_i2c.add_result = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, i2c_add_device(&device));
    TEST_ASSERT_EQUAL_UINT(2, fake_i2c.add_calls);
}

static void test_remove_clears_handle_only_on_success(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2c_remove_device(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2c_remove_device(&device));
    TEST_ASSERT_EQUAL_UINT(0, fake_i2c.remove_calls);
    device.handle = fake_device;
    fake_i2c.remove_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, i2c_remove_device(&device));
    TEST_ASSERT_EQUAL_PTR(fake_device, device.handle);
    TEST_ASSERT_EQUAL_PTR(fake_device, fake_i2c.last_device);
    fake_i2c.remove_result = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, i2c_remove_device(&device));
    TEST_ASSERT_NULL(device.handle);
}

static void test_reads_reject_invalid_arguments_without_driver_calls(void) {
    uint8_t value;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2c_read_reg8(NULL, 0, &value));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2c_read_reg8(&device, 0, &value));
    device.handle = fake_device;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2c_read_reg8(&device, 0, NULL));
    TEST_ASSERT_EQUAL_UINT(0, fake_i2c.read_calls);
}

static void test_zero_length_read_is_a_noop(void) {
    device.handle = fake_device;
    TEST_ASSERT_EQUAL(ESP_OK, i2c_read_regs(&device, 0x20, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0, fake_i2c.read_calls);
}

static void test_read_uses_one_combined_register_transaction(void) {
    device.handle = fake_device;
    const uint8_t expected[] = {0x12, 0x00, 0xFF};
    memcpy(fake_i2c.read_data, expected, sizeof(expected));
    uint8_t data[3] = {0};
    TEST_ASSERT_EQUAL(ESP_OK, i2c_read_regs(&device, 0x20, data, sizeof(data)));
    TEST_ASSERT_EQUAL_UINT(1, fake_i2c.read_calls);
    TEST_ASSERT_EQUAL_PTR(fake_device, fake_i2c.last_device);
    TEST_ASSERT_EQUAL_UINT(1, fake_i2c.write_size);
    TEST_ASSERT_EQUAL_HEX8(0x20, fake_i2c.write_data[0]);
    TEST_ASSERT_EQUAL_UINT(3, fake_i2c.read_size);
    TEST_ASSERT_EQUAL_INT(100, fake_i2c.timeout_ms);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, data, sizeof(data));
}

static void test_read_propagates_driver_error(void) {
    device.handle = fake_device;
    fake_i2c.read_result = ESP_ERR_TIMEOUT;
    uint8_t value = 0xA5;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, i2c_read_reg8(&device, 0x20, &value));
    TEST_ASSERT_EQUAL_UINT(1, fake_i2c.read_calls);
    TEST_ASSERT_EQUAL_HEX8(0xA5, value);
}

static void test_writes_reject_invalid_arguments_without_driver_calls(void) {
    const uint8_t value = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2c_write_regs(NULL, 0, &value, 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      i2c_write_regs(&device, 0, &value, 1));
    device.handle = fake_device;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2c_write_regs(&device, 0, NULL, 1));
    TEST_ASSERT_EQUAL_UINT(0, fake_i2c.write_calls);
}

static void test_write_prepends_register_to_payload(void) {
    device.handle = fake_device;
    const uint8_t data[] = {0x12, 0x00, 0xFF};
    const uint8_t expected[] = {0x20, 0x12, 0x00, 0xFF};
    TEST_ASSERT_EQUAL(ESP_OK,
                      i2c_write_regs(&device, 0x20, data, sizeof(data)));
    TEST_ASSERT_EQUAL_UINT(1, fake_i2c.write_calls);
    TEST_ASSERT_EQUAL_PTR(fake_device, fake_i2c.last_device);
    TEST_ASSERT_EQUAL_UINT(2, fake_i2c.buffer_count);
    TEST_ASSERT_EQUAL_UINT(4, fake_i2c.write_size);
    TEST_ASSERT_EQUAL_INT(100, fake_i2c.timeout_ms);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, fake_i2c.write_data,
                                 sizeof(expected));
}

static void test_zero_length_write_sends_register_only(void) {
    device.handle = fake_device;
    TEST_ASSERT_EQUAL(ESP_OK, i2c_write_regs(&device, 0x20, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(1, fake_i2c.write_calls);
    TEST_ASSERT_EQUAL_UINT(1, fake_i2c.buffer_count);
    TEST_ASSERT_EQUAL_UINT(1, fake_i2c.write_size);
    TEST_ASSERT_EQUAL_HEX8(0x20, fake_i2c.write_data[0]);
}

static void test_set_reg8_sends_value_and_propagates_error(void) {
    device.handle = fake_device;
    const uint8_t expected[] = {0x20, 0xA5};
    TEST_ASSERT_EQUAL(ESP_OK, i2c_set_reg8(&device, 0x20, 0xA5));
    TEST_ASSERT_EQUAL_UINT(2, fake_i2c.write_size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, fake_i2c.write_data, 2);
    fake_i2c.write_result = ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, i2c_set_reg8(&device, 0x20, 0xA5));
}

static void test_whoami_accepts_expected_value(void) {
    device.handle = fake_device;
    fake_i2c.read_data[0] = device.expected_whoami;
    TEST_ASSERT_EQUAL(ESP_OK, i2c_whoami_check(&device));
    TEST_ASSERT_EQUAL_HEX8(device.whoami_reg, fake_i2c.write_data[0]);
    TEST_ASSERT_EQUAL_UINT(1, fake_i2c.read_size);
}

static void test_whoami_rejects_mismatch_and_propagates_read_error(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2c_whoami_check(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2c_whoami_check(&device));
    device.handle = fake_device;
    fake_i2c.read_data[0] = 0xFF;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, i2c_whoami_check(&device));
    fake_i2c.read_result = ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, i2c_whoami_check(&device));
}

static void test_deinit_clears_cache_and_can_be_repeated(void) {
    fake_i2c.probe_results[0x42] = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, i2c_init());
    i2c_deinit();
    i2c_deinit();
    TEST_ASSERT_EQUAL_UINT(1, fake_i2c.delete_calls);
    size_t count = 99;
    uint8_t addresses[16];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      i2c_get_detected_devices(addresses, 16, &count));
    fake_i2c_reset();
    TEST_ASSERT_EQUAL(ESP_OK, i2c_init());
    TEST_ASSERT_EQUAL(ESP_OK, i2c_get_detected_devices(addresses, 16, &count));
    TEST_ASSERT_EQUAL_UINT(0, count);
}

static void test_failed_deinit_keeps_bus_available_for_retry(void) {
    TEST_ASSERT_EQUAL(ESP_OK, i2c_init());
    fake_i2c.delete_result = ESP_FAIL;
    i2c_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, i2c_init());
    TEST_ASSERT_EQUAL_UINT(1, fake_i2c.create_calls);
    fake_i2c.delete_result = ESP_OK;
    i2c_deinit();
    TEST_ASSERT_EQUAL_UINT(2, fake_i2c.delete_calls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2c_add_device(&device));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_requires_initialized_bus);
    RUN_TEST(test_initialization_configures_bus_and_scans_valid_addresses);
    RUN_TEST(test_initialization_is_idempotent);
    RUN_TEST(test_failed_initialization_can_be_retried);
    RUN_TEST(test_scan_caches_only_acknowledged_devices);
    RUN_TEST(test_scan_truncates_cache_and_respects_output_capacity);
    RUN_TEST(test_scan_rejects_null_count);
    RUN_TEST(test_add_device_configures_address_and_speed_once);
    RUN_TEST(test_add_device_propagates_failure_and_allows_retry);
    RUN_TEST(test_remove_clears_handle_only_on_success);
    RUN_TEST(test_reads_reject_invalid_arguments_without_driver_calls);
    RUN_TEST(test_zero_length_read_is_a_noop);
    RUN_TEST(test_read_uses_one_combined_register_transaction);
    RUN_TEST(test_read_propagates_driver_error);
    RUN_TEST(test_writes_reject_invalid_arguments_without_driver_calls);
    RUN_TEST(test_write_prepends_register_to_payload);
    RUN_TEST(test_zero_length_write_sends_register_only);
    RUN_TEST(test_set_reg8_sends_value_and_propagates_error);
    RUN_TEST(test_whoami_accepts_expected_value);
    RUN_TEST(test_whoami_rejects_mismatch_and_propagates_read_error);
    RUN_TEST(test_deinit_clears_cache_and_can_be_repeated);
    RUN_TEST(test_failed_deinit_keeps_bus_available_for_retry);
    return UNITY_END();
}
