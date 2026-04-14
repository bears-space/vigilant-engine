#include <unistd.h>
#include "esp_log.h"
#include "vigilant.h"
#include "status_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// static const char *TAG = "app_main";

void app_main(void)
{
    VigilantConfig VgConfig = {
        .unique_component_name = "Vigilant ESP Test",
        .network_mode = NW_MODE_APSTA
    };
    ESP_ERROR_CHECK(vigilant_init(VgConfig));

    VigilantI2CDevice device = {
        .address = 0x18,
        .whoami_reg = 0x0F,
        .expected_whoami = 0x32,
        .handle = NULL,
    };

    ESP_ERROR_CHECK(vigilant_i2c_add_device(&device));
    esp_err_t i2cerr = vigilant_i2c_whoami_check(&device); // WHOAMI check
    if (i2cerr != ESP_OK) {
        status_led_set_state(STATUS_STATE_ERROR);
        ESP_LOGE("app_main", "I2C WHOAMI check failed: %s", esp_err_to_name(i2cerr));
    } else {
        status_led_set_state(STATUS_STATE_INFO);
        ESP_LOGI("app_main", "I2C WHOAMI check succeeded");
        uint8_t value = 0;
        ESP_ERROR_CHECK(vigilant_i2c_read_reg8(&device, 0x27, &value)); // Reading a register (e.g. control register)
        ESP_LOGI("app_main i2c test", "reg 0x27 value: 0x%02X", value);
        ESP_ERROR_CHECK(vigilant_i2c_remove_device(&device)); // Removing a device
    }
}
