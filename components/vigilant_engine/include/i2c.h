#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t i2c_init(void);
esp_err_t i2c_add_device(uint16_t address, i2c_master_dev_handle_t *out_dev);
void i2c_deinit(void);

#ifdef __cplusplus
}
#endif
