#pragma once

#include "esp_err.h"
#include "vigilant_i2c_device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NW_MODE_AP = 0,
    NW_MODE_STA,
    NW_MODE_APSTA
} NW_MODE;

typedef struct {
    char unique_component_name[32];
    NW_MODE network_mode;
} VigilantConfig;

typedef struct {
    char unique_component_name[32];
    NW_MODE network_mode;
    char mac[18];            // e.g. "AA:BB:CC:DD:EE:FF"
    char ap_ssid[33];        // configured AP SSID (null-terminated)
    char sta_ssid[33];       // configured STA SSID (null-terminated)
    char ip_sta[16];         // current STA IPv4 (or "0.0.0.0")
    char ip_ap[16];          // current AP IPv4 (or "0.0.0.0")
} VigilantInfo;

esp_err_t vigilant_init(VigilantConfig VgConfig);
esp_err_t vigilant_get_info(VigilantInfo *info);
esp_err_t vigilant_i2c_add_device(VigilantI2CDevice *device);
esp_err_t vigilant_i2c_remove_device(VigilantI2CDevice *device);
esp_err_t vigilant_i2c_set_reg8(VigilantI2CDevice *device, uint8_t reg, uint8_t value);
esp_err_t vigilant_i2c_read_reg8(VigilantI2CDevice *device, uint8_t reg, uint8_t *value);
esp_err_t vigilant_i2c_whoami_check(VigilantI2CDevice *device);

#ifdef __cplusplus
}
#endif
