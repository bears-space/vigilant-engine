#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "telemetry.h"

static const char* TAG = "example_sensor";

int count = 0;

static void example_task(void* arg) {
    TickType_t last_wake_time = xTaskGetTickCount();
    sensor_channel_t* channel = arg;

    // Check if the channel and its config are valid
    if (channel == NULL || channel->config == NULL ||
        channel->config->name == NULL) {
        ESP_LOGE(TAG, "Invalid task argument");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = ESP_OK;

    while (true) {
        ESP_LOGI(TAG, "%d: Example sensor task running, channel: %s", count++,
                 channel->config->name);

        // write to queue to simulate a measurement being sent
        sensor_measurement_t measurement = {
            .timestamp_us = (uint64_t)esp_timer_get_time(),
            .flags = MEASUREMENT_VALID,
        };

        if (err != ESP_OK) {
            measurement.flags = MEASUREMENT_ERROR;

            ESP_LOGE(TAG, "IMU read failed: %s", esp_err_to_name(err));
        }

        err = pipeline_submit_measurement(channel, &measurement, 0);

        if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Measurement dropped, total: %lu",
                     (unsigned long)channel->dropped_measurements);
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "Submission failed: %s", esp_err_to_name(err));
        }

        xTaskDelayUntil(
            &last_wake_time,
            pdMS_TO_TICKS(100)  // THIS MUST BE COMPLIANT WITH
                                // CONFIG_FREERTOS_HZ AND nominal_period_us
        );
    }
}

esp_err_t example_sensor_init(void) {
    // create a config for the example sensor channel we want to create
    static const sensor_channel_config_t example_config = {
        .id = 1,  // channel id, must be unique for each channel
        .name = "Example IMU Sensor",
        .measurement_type = MEASUREMENT_IMU,
        .nominal_period_us = 100000,
    };

    esp_err_t err = pipeline_create_channel(&example_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create sensor channel: %s",
                 esp_err_to_name(err));
        return err;
    }

    // now we can find the channel we just created and start a task for it
    sensor_channel_t* channel = NULL;

    err = sensor_registry_find(get_sensor_registry(), 1, &channel);

    if (err != ESP_OK || channel == NULL) {
        ESP_LOGE(TAG, "Failed to find channel: %s", esp_err_to_name(err));

        return err != ESP_OK ? err : ESP_ERR_NOT_FOUND;
    }

    // start a FreeRTOS task for the example sensor channel
    BaseType_t result = xTaskCreate(example_task, TAG, 2048, channel, 5, NULL);

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create example task");
    }

    return ESP_OK;
}
