#pragma once

#include "esp_err.h"
#include "vigilant_i2c_device.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t i2c_init(void);
esp_err_t i2c_add_device(VigilantI2CDevice *device);
esp_err_t i2c_remove_device(VigilantI2CDevice *device);
esp_err_t i2c_set_reg8(VigilantI2CDevice *device, uint8_t reg, uint8_t value);
esp_err_t i2c_read_reg8(VigilantI2CDevice *device, uint8_t reg, uint8_t *value);
esp_err_t i2c_whoami_check(VigilantI2CDevice *device);
void i2c_deinit(void);

#ifdef __cplusplus
}
#endif
