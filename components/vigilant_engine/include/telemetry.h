#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef enum {
    SENSOR_IMU,
    SENSOR_BAROMETER,
    SENSOR_GNSS,
    SENSOR_ADC,
    SENSOR_COUNT
} sensor_id_t;

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
    sensor_id_t sensor_id;
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

esp_err_t pipeline_init(void);
esp_err_t pipeline_deinit(void);
