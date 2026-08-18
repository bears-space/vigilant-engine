#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "vigilant_i2c_device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { NW_MODE_AP = 0, NW_MODE_STA, NW_MODE_APSTA } NW_MODE;

typedef struct {
    char unique_component_name[32];
    NW_MODE network_mode;
    bool is_master;
} VigilantConfig;

typedef struct {
    char unique_component_name[32];
    NW_MODE network_mode;
    bool is_master;
    char mac[18];       // e.g. "AA:BB:CC:DD:EE:FF"
    char ap_ssid[33];   // configured AP SSID (null-terminated)
    char sta_ssid[33];  // configured STA SSID (null-terminated)
    char ip_sta[16];    // current STA IPv4 (or "0.0.0.0")
    char ip_ap[16];     // current AP IPv4 (or "0.0.0.0")
} VigilantInfo;

typedef struct {
    bool enabled;
    uint8_t sda_io;
    uint8_t scl_io;
    uint32_t frequency_hz;
    uint8_t added_device_count;
    VigilantI2CDevice
        added_devices[8];  // fixed-size array for simplicity; adjust as needed
    uint8_t detected_device_count;
    uint8_t detected_devices[16];  // 7-bit addresses detected on the bus
} VigilantI2cInfo;

#define VIGILANT_DEVICE_MAGIC "vigilant-engine-device-v1"
#define VIGILANT_DEVICE_MAGIC_HEADER "X-Vigilant-Device"

typedef enum {
    VIGILANT_WIFI_DEVICE_IDENTITY_UNKNOWN = 0,
    VIGILANT_WIFI_DEVICE_IDENTITY_VIGILANT,
    VIGILANT_WIFI_DEVICE_IDENTITY_OTHER,
} VigilantWifiDeviceIdentity;

typedef struct {
    bool is_vigilant_device;  // compatibility flag; use identity when unknown
                              // vs other matters
    VigilantWifiDeviceIdentity identity;
    char name[32];
    char mac[18];  // e.g. "AA:BB:CC:DD:EE:FF"
    uint32_t address;
} VigilantWifiDevice;

#define VIGILANT_WIFI_MAX_CONNECTED_DEVICES 8

typedef struct {
    NW_MODE network_mode;
    char mac[18];                     // e.g. "AA:BB:CC:DD:EE:FF"
    char ap_ssid[33];                 // configured AP SSID (null-terminated)
    char sta_ssid[33];                // configured STA SSID (null-terminated)
    char ip_sta[16];                  // current STA IPv4 (or "0.0.0.0")
    char ip_ap[16];                   // current AP IPv4 (or "0.0.0.0")
    uint8_t connected_devices_count;  // number of currently connected WiFi
                                      // clients (for AP mode)
    VigilantWifiDevice connected_devices[VIGILANT_WIFI_MAX_CONNECTED_DEVICES];

} VigilantWiFiInfo;

esp_err_t vigilant_init(VigilantConfig VgConfig);
esp_err_t vigilant_get_info(VigilantInfo* info);
esp_err_t vigilant_get_i2cinfo(VigilantI2cInfo* info);
esp_err_t vigilant_get_wifiinfo(VigilantWiFiInfo* info);
esp_err_t vigilant_update_wifi_device_identity(
    const char* mac, uint32_t address, VigilantWifiDeviceIdentity identity,
    const char* name);
esp_err_t vigilant_i2c_add_device(VigilantI2CDevice* device);
esp_err_t vigilant_i2c_remove_device(VigilantI2CDevice* device);
esp_err_t vigilant_i2c_set_reg8(VigilantI2CDevice* device, uint8_t reg,
                                uint8_t value);
esp_err_t vigilant_i2c_read_reg8(VigilantI2CDevice* device, uint8_t reg,
                                 uint8_t* value);
esp_err_t vigilant_i2c_whoami_check(VigilantI2CDevice* device);

#ifdef __cplusplus
}
#endif
