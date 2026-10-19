#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log_level.h"

#ifdef __cplusplus
extern "C" {
#endif

// esp_err_t status_led_init(const status_led_config_t *cfg);
// esp_err_t status_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);
// esp_err_t status_led_off(void);
// esp_err_t status_led_deinit(void);
esp_err_t init_led();
esp_err_t status_led_enable_log_feedback(void);

// Use ESP_LOG_NONE to turn the LED off.
esp_err_t status_led_set_state(esp_log_level_t state);

esp_err_t status_led_blink_stop(void);

#ifdef __cplusplus
}
#endif
