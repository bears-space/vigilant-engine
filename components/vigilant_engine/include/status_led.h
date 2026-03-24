#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STATUS_STATE_INFO,
    STATUS_STATE_WARNING,
    STATUS_STATE_ERROR,
    STATUS_STATE_OFF
} status_state_t;

//esp_err_t status_led_init(const status_led_config_t *cfg);
//esp_err_t status_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);
//esp_err_t status_led_off(void);
//esp_err_t status_led_deinit(void);
esp_err_t configure_led();

esp_err_t status_led_set_state(status_state_t state);

// optional nice-to-have helpers
esp_err_t status_led_blink_start(uint32_t on_ms, uint32_t off_ms, uint8_t led_gpio);
esp_err_t status_led_blink_stop(void);

#ifdef __cplusplus
}
#endif
