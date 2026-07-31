#include "telemetry.h"

#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char* TAG_PIPELINE = "pipeline";

sensor_channel_registry_t sensor_registry = {0};

esp_err_t sensor_registry_init(sensor_channel_registry_t* registry) {
    if (registry == NULL) {
        ESP_LOGE(TAG_PIPELINE, "sensor_registry_init: registry is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    memset(registry, 0, sizeof(*registry));

    registry->mutex = xSemaphoreCreateMutex();

    if (registry->mutex == NULL) {
        ESP_LOGE(
            TAG_PIPELINE,
            "sensor_registry_init: failed to create mutex, out of memory?");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

sensor_channel_registry_t* get_sensor_registry(void) {
    return &sensor_registry;
}

static esp_err_t sensor_registry_grow(sensor_channel_registry_t* registry) {
    if (registry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t new_capacity =
        registry->capacity == 0 ? 4 : registry->capacity * 2;

    if (new_capacity < registry->capacity ||
        new_capacity > SIZE_MAX / sizeof(*registry->items)) {
        return ESP_ERR_NO_MEM;
    }

    sensor_channel_t** new_items =
        pvPortMalloc(new_capacity * sizeof(*new_items));

    if (new_items == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (registry->items != NULL) {
        memcpy(new_items, registry->items,
               registry->count * sizeof(*new_items));

        vPortFree(registry->items);
    }

    registry->items = new_items;
    registry->capacity = new_capacity;

    return ESP_OK;
}

esp_err_t sensor_registry_add(sensor_channel_registry_t* registry,
                              sensor_channel_t* channel) {
    if (registry == NULL || registry->mutex == NULL || channel == NULL) {
        ESP_LOGE(TAG_PIPELINE, "sensor_registry_add: invalid argument(s)");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(registry->mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG_PIPELINE, "sensor_registry_add: failed to take mutex");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_OK;

    for (size_t i = 0; i < registry->count; i++) {
        if (registry->items[i]->config->id == channel->config->id) {
            result = ESP_ERR_INVALID_STATE;  // a channel with that ID is
                                             // already registered
            ESP_LOGE(
                TAG_PIPELINE,
                "sensor_registry_add: channel with ID %d already registered",
                channel->config->id);
            goto cleanup;
        }
    }

    if (registry->count == registry->capacity) {
        result = sensor_registry_grow(registry);

        if (result != ESP_OK) {
            ESP_LOGE(TAG_PIPELINE,
                     "sensor_registry_add: failed to grow registry: %s",
                     esp_err_to_name(result));
            goto cleanup;
        }
    }

    registry->items[registry->count] = channel;
    registry->count++;

cleanup:
    xSemaphoreGive(registry->mutex);
    return result;
}

esp_err_t sensor_registry_deinit(sensor_channel_registry_t* registry) {
    if (registry == NULL) {
        ESP_LOGE(TAG_PIPELINE, "sensor_registry_deinit: registry is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (registry->count != 0) {
        ESP_LOGE(TAG_PIPELINE, "sensor_registry_deinit: registry is not empty");
        return ESP_ERR_INVALID_STATE;
    }

    if (registry->items != NULL) {
        vPortFree(registry->items);
        registry->items = NULL;
    }

    if (registry->mutex != NULL) {
        vSemaphoreDelete(registry->mutex);
        registry->mutex = NULL;
    }

    registry->count = 0;
    registry->capacity = 0;

    return ESP_OK;
}

esp_err_t pipeline_init(void) {
    esp_err_t err = sensor_registry_init(&sensor_registry);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_PIPELINE, "Failed to initialize sensor registry: %s",
                 esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

esp_err_t pipeline_create_channel(const sensor_channel_config_t* config) {
    if (config == NULL) {
        ESP_LOGE(TAG_PIPELINE, "pipeline_create_channel: config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG_PIPELINE, "Creating sensor channel: ID=%d, Name=%s",
             config->id, config->name);

    sensor_channel_t* channel = calloc(1, sizeof(*channel));

    if (channel == NULL) {
        ESP_LOGE(TAG_PIPELINE, "Failed to allocate sensor channel");
        return ESP_ERR_NO_MEM;
    }

    channel->config = config;
    channel->queue = xQueueCreate(10, sizeof(sensor_measurement_t));
    channel->fusion_task = NULL;
    channel->next_sequence = 0;
    channel->dropped_measurements = 0;

    if (channel->queue == NULL) {
        ESP_LOGE(TAG_PIPELINE, "Failed to create channel queue");
        free(channel);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = sensor_registry_add(&sensor_registry, channel);

    if (err != ESP_OK) {
        ESP_LOGE(TAG_PIPELINE, "Failed to register sensor channel: %s",
                 esp_err_to_name(err));

        vQueueDelete(channel->queue);
        free(channel);
        return err;
    }

    return ESP_OK;
}

esp_err_t sensor_registry_find(sensor_channel_registry_t* registry,
                               sensor_channel_id_t id,
                               sensor_channel_t** out_channel) {
    if (registry == NULL || registry->mutex == NULL || out_channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_channel = NULL;

    if (xSemaphoreTake(registry->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_ERR_NOT_FOUND;

    for (size_t i = 0; i < registry->count; i++) {
        if (registry->items[i]->config->id == id) {
            *out_channel = registry->items[i];
            result = ESP_OK;
            break;
        }
    }

    xSemaphoreGive(registry->mutex);
    return result;
}

esp_err_t pipeline_submit_measurement(sensor_channel_t* channel,
                                      const sensor_measurement_t* measurement,
                                      TickType_t timeout) {
    if (channel == NULL || channel->config == NULL || channel->queue == NULL ||
        measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Copy the caller's measurement so we can safely add
     * pipeline-controlled metadata.
     */
    sensor_measurement_t queued_measurement = *measurement;

    queued_measurement.sensor_id = channel->config->id;

    queued_measurement.type = channel->config->measurement_type;

    /*
     * Increment before queueing. If the queue is full, there will be
     * a sequence gap, making dropped measurements detectable.
     */
    queued_measurement.sequence = channel->next_sequence++;

    if (xQueueSend(channel->queue, &queued_measurement, timeout) != pdTRUE) {
        channel->dropped_measurements++;
        return ESP_ERR_TIMEOUT;
    }

    /*
     * Wake the fusion task and tell it which channel has new data.
     */
    if (channel->fusion_task != NULL &&
        channel->config->notification_bit != 0) {
        xTaskNotify(channel->fusion_task, channel->config->notification_bit,
                    eSetBits);
    }

    return ESP_OK;
}

esp_err_t pipeline_destroy_channel(uint8_t sensor_id) {
    // Implementation for destroying a sensor channel
    return ESP_OK;
}

esp_err_t pipeline_deinit(void) {
    if (sensor_registry_deinit(&sensor_registry) != ESP_OK) {
        ESP_LOGE(TAG_PIPELINE, "Failed to deinitialize sensor registry");
        return ESP_ERR_INVALID_STATE;  // registry not empty
    }
    return ESP_OK;
}
