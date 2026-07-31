#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

typedef enum {
    MEASUREMENT_IMU,
    MEASUREMENT_BAROMETER,
    MEASUREMENT_GNSS,
    MEASUREMENT_ADC
} measurement_type_t;

typedef enum {
    MEASUREMENT_VALID = 1 << 0,
    MEASUREMENT_CALIBRATED = 1 << 1,
    MEASUREMENT_SATURATED = 1 << 2,
    MEASUREMENT_ERROR = 1 << 3
} measurement_flags_t;

typedef struct {
    uint8_t sensor_id;
    measurement_type_t type;

    uint64_t timestamp_us;
    uint32_t sequence;
    uint32_t flags;

    union {
        struct {
            float acceleration[3];
            float angular_velocity[3];
        } imu;

        struct {
            float pressure_pa;
            float temperature_c;
        } barometer;

        struct {
            double latitude_deg;
            double longitude_deg;
            float altitude_m;
            float velocity_mps;
        } gnss;

        struct {
            float voltage_v;
            float current_a;
        } adc;
    } data;
} sensor_measurement_t;

typedef struct {
    uint8_t id;
    const char* name;
    measurement_type_t measurement_type;

    uint32_t nominal_period_us;
    uint32_t notification_bit;
} sensor_channel_config_t;

typedef struct {
    const sensor_channel_config_t* config;

    QueueHandle_t queue;
    TaskHandle_t fusion_task;

    uint32_t next_sequence;
    uint32_t dropped_measurements;
} sensor_channel_t;

typedef struct {
    sensor_channel_t** items;
    size_t count;
    size_t capacity;
    SemaphoreHandle_t mutex;
} sensor_channel_registry_t;

typedef uint32_t sensor_channel_id_t;

esp_err_t pipeline_submit_measurement(sensor_channel_t* channel,
                                      const sensor_measurement_t* measurement,
                                      TickType_t timeout);

sensor_channel_registry_t* get_sensor_registry(void);
esp_err_t pipeline_init(void);
esp_err_t pipeline_create_channel(const sensor_channel_config_t* config);
esp_err_t sensor_registry_find(sensor_channel_registry_t* registry,
                               sensor_channel_id_t id,
                               sensor_channel_t** out_channel);
esp_err_t pipeline_deinit(void);
