#include "telemetry.h"

esp_err_t example_sensor_init(void) {
    pipeline_create_channel(&(sensor_channel_config_t){
        .id = 1,
        .name = "Example IMU Sensor",
        .measurement_type = MEASUREMENT_IMU,
        .nominal_period_us = 100000,  // 10 Hz
    });

    return ESP_OK;
}
