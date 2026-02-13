#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int gpio;                 // Data pin
    uint32_t resolution_hz;   // e.g. 10*1000*1000
    uint32_t max_leds;        // usually 1
    bool invert_out;          // usually false
    bool with_dma;            // usually false
    uint32_t mem_block_symbols; // e.g. 64
} status_led_config_t;

esp_err_t status_led_init(const status_led_config_t *cfg);
esp_err_t status_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);
esp_err_t status_led_off(void);
esp_err_t status_led_deinit(void);

// optional nice-to-have helpers
esp_err_t status_led_blink_start(uint8_t r, uint8_t g, uint8_t b, uint32_t on_ms, uint32_t off_ms);
esp_err_t status_led_blink_stop(void);

#ifdef __cplusplus
}
#endif
