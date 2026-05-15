#include "vigilant.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "http_server.h"
#include "i2c.h"
#include "lwip/inet.h"
#include "master.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "status_led.h"
#include "websocket.h"

static const char* TAG = "vigilant";
#if CONFIG_VE_ENABLE_I2C
static const size_t MAX_I2C_INFO_DEVICES = 8;
#endif

static esp_netif_t* s_netif_sta = NULL;
static esp_netif_t* s_netif_ap = NULL;
static VigilantConfig s_cfg = {0};
static TimerHandle_t sta_reconnect_timer;
typedef struct {
    bool in_use;
    char mac[18];
    uint32_t address;
    VigilantWifiDeviceIdentity identity;
    char name[32];
} VigilantWifiIdentityCacheEntry;

static SemaphoreHandle_t s_wifi_identity_mutex = NULL;
static VigilantWifiIdentityCacheEntry
    s_wifi_identity_cache[VIGILANT_WIFI_MAX_CONNECTED_DEVICES] = {0};
#if CONFIG_VE_ENABLE_I2C
static VigilantI2CDevice s_i2c_devices[8] = {0};
static size_t s_i2c_device_count = 0;
#endif

static wifi_config_t sta_cfg = {.sta = {
                                    .ssid = CONFIG_VE_STA_SSID,
                                    .password = CONFIG_VE_STA_PASSWORD,
                                    .channel = 1,
                                    .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                                }};

static wifi_config_t ap_cfg = {
    .ap = {.ssid = "starstreak-SETUP",  // made unique in later
           .ssid_len = 0,
           .password = CONFIG_VE_AP_PASSWORD,  // leer => open AP, dann aber
                                               // auch authmode anpassen
           .channel = 1,
           .max_connection = 4,
           .authmode = WIFI_AUTH_WPA2_PSK}};

#if CONFIG_VE_ENABLE_I2C
static int registered_i2c_device_index(const VigilantI2CDevice* device) {
    if (!device) {
        return -1;
    }

    for (size_t i = 0; i < s_i2c_device_count; ++i) {
        if (s_i2c_devices[i].address == device->address &&
            s_i2c_devices[i].whoami_reg == device->whoami_reg &&
            s_i2c_devices[i].expected_whoami == device->expected_whoami) {
            return (int)i;
        }
    }

    return -1;
}

static void sync_registered_i2c_device(const VigilantI2CDevice* device) {
    if (!device) {
        return;
    }

    int existing_index = registered_i2c_device_index(device);
    if (existing_index >= 0) {
        s_i2c_devices[existing_index] = *device;
        return;
    }

    if (s_i2c_device_count >= MAX_I2C_INFO_DEVICES) {
        ESP_LOGW(TAG,
                 "I2C device registry full; device 0x%02X will not be exposed "
                 "via /i2cinfo",
                 (unsigned int)device->address);
        return;
    }

    s_i2c_devices[s_i2c_device_count++] = *device;
}

static void remove_registered_i2c_device(const VigilantI2CDevice* device) {
    int existing_index = registered_i2c_device_index(device);
    if (existing_index < 0) {
        return;
    }

    for (size_t i = (size_t)existing_index + 1; i < s_i2c_device_count; ++i) {
        s_i2c_devices[i - 1] = s_i2c_devices[i];
    }

    s_i2c_device_count--;
    memset(&s_i2c_devices[s_i2c_device_count], 0, sizeof(s_i2c_devices[0]));
}
#endif

static void ap_cfg_fixup(wifi_config_t* cfg) {
    // Wenn password leer ist: auth auf OPEN setzen, sonst meckert IDF rum
    if (cfg->ap.password[0] == '\0') {
        cfg->ap.authmode = WIFI_AUTH_OPEN;
    }
}

static bool wifi_device_identity_is_valid(VigilantWifiDeviceIdentity identity) {
    return identity == VIGILANT_WIFI_DEVICE_IDENTITY_UNKNOWN ||
           identity == VIGILANT_WIFI_DEVICE_IDENTITY_VIGILANT ||
           identity == VIGILANT_WIFI_DEVICE_IDENTITY_OTHER;
}

static void copy_string(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static void format_mac_address(char* dst, size_t dst_size,
                               const uint8_t mac[6]) {
    if (!dst || dst_size == 0 || !mac) {
        return;
    }

    snprintf(dst, dst_size, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
}

static esp_err_t wifi_identity_cache_init(void) {
    if (s_wifi_identity_mutex) {
        return ESP_OK;
    }

    s_wifi_identity_mutex = xSemaphoreCreateMutex();
    if (!s_wifi_identity_mutex) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static int wifi_identity_cache_find_locked(const char* mac) {
    if (!mac || mac[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < VIGILANT_WIFI_MAX_CONNECTED_DEVICES; ++i) {
        if (s_wifi_identity_cache[i].in_use &&
            strcasecmp(s_wifi_identity_cache[i].mac, mac) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static int wifi_identity_cache_free_slot_locked(void) {
    for (size_t i = 0; i < VIGILANT_WIFI_MAX_CONNECTED_DEVICES; ++i) {
        if (!s_wifi_identity_cache[i].in_use) {
            return (int)i;
        }
    }

    return 0;
}

static bool wifi_identity_cache_get(const char* mac,
                                    VigilantWifiIdentityCacheEntry* out_entry) {
    if (!mac || !out_entry || !s_wifi_identity_mutex) {
        return false;
    }

    if (xSemaphoreTake(s_wifi_identity_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    int index = wifi_identity_cache_find_locked(mac);
    if (index >= 0) {
        *out_entry = s_wifi_identity_cache[index];
    }

    xSemaphoreGive(s_wifi_identity_mutex);
    return index >= 0;
}

esp_err_t vigilant_update_wifi_device_identity(
    const char* mac, uint32_t address, VigilantWifiDeviceIdentity identity,
    const char* name) {
    if (!mac || mac[0] == '\0' || !wifi_device_identity_is_valid(identity)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = wifi_identity_cache_init();
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(s_wifi_identity_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    int index = wifi_identity_cache_find_locked(mac);
    if (index < 0) {
        index = wifi_identity_cache_free_slot_locked();
        memset(&s_wifi_identity_cache[index], 0,
               sizeof(s_wifi_identity_cache[index]));
    }

    VigilantWifiIdentityCacheEntry* entry = &s_wifi_identity_cache[index];
    entry->in_use = true;
    copy_string(entry->mac, sizeof(entry->mac), mac);
    entry->address = address;
    entry->identity = identity;

    if (identity == VIGILANT_WIFI_DEVICE_IDENTITY_VIGILANT) {
        copy_string(entry->name, sizeof(entry->name), name);
    } else {
        entry->name[0] = '\0';
    }

    xSemaphoreGive(s_wifi_identity_mutex);
    return ESP_OK;
}

void reboot_to_recovery(void) {
    const esp_partition_t* factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (!factory) return;

    esp_ota_set_boot_partition(factory);
    esp_restart();
}

static void sta_reconnect_callback(TimerHandle_t xTimer) { esp_wifi_connect(); }

static void wifi_evt(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* e = (wifi_event_sta_disconnected_t*)data;
        ESP_LOGW("wifi", "STA disconnected, reason=%d", e->reason);
        status_led_set_state(STATUS_STATE_WARNING);
        xTimerReset(sta_reconnect_timer, 0);
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* e = (ip_event_got_ip_t*)data;
        ESP_LOGI("wifi", "Got IP: " IPSTR, IP2STR(&e->ip_info.ip));
    }
}

static esp_err_t wifi_init_once(void) {
    static bool inited = false;
    if (inited) return ESP_OK;

    // Netifs
    s_netif_sta = esp_netif_create_default_wifi_sta();
    s_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_evt, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_evt, NULL));

    sta_reconnect_timer = xTimerCreate(
        "wifi_reconnect", pdMS_TO_TICKS(CONFIG_VE_STA_RECONNECT_INTERVAL_MS),
        pdFALSE, (void*)0, sta_reconnect_callback);

    inited = true;
    return ESP_OK;
}

static esp_err_t wifi_apply_mode(NW_MODE mode, wifi_config_t* sta,
                                 wifi_config_t* ap) {
    // Stop kann beim ersten Mal "not started" sein -> nicht dramatisch
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED &&
        err != ESP_ERR_WIFI_NOT_INIT) {
        return err;
    }

    wifi_mode_t m;
    if (mode == NW_MODE_STA)
        m = WIFI_MODE_STA;
    else if (mode == NW_MODE_AP)
        m = WIFI_MODE_AP;
    else if (mode == NW_MODE_APSTA)
        m = WIFI_MODE_APSTA;
    else
        return ESP_ERR_INVALID_ARG;

    ESP_ERROR_CHECK(esp_wifi_set_mode(m));

    if (mode == NW_MODE_STA || mode == NW_MODE_APSTA) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, sta));
    }
    if (mode == NW_MODE_AP || mode == NW_MODE_APSTA) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, ap));
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    if (mode == NW_MODE_STA || mode == NW_MODE_APSTA) {
        ESP_ERROR_CHECK(esp_wifi_connect());  // AP-only: NICHT connecten
    }

    return ESP_OK;
}

esp_err_t vigilant_init(VigilantConfig VgConfig) {
    bool initializedSuccessfully =
        true;  // Assume success until a failure occurs
    uint8_t mac[6];

    // throw compiler error is is-master is true but network mode is not APSTA
    // (since only APSTA supports both AP and STA)
    if (VgConfig.is_master && VgConfig.network_mode != NW_MODE_APSTA) {
        ESP_LOGE(TAG,
                 "Invalid configuration: is_master=true requires "
                 "network_mode=NW_MODE_APSTA");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Init NVS");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    esp_err_t err = configure_led();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure status LED: %s",
                 esp_err_to_name(err));
        initializedSuccessfully = false;
    }

    // Capture ESP-IDF logs early so they can be replayed to websocket clients
    websocket_init_log_capture();

    ret = wifi_identity_cache_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi identity cache: %s",
                 esp_err_to_name(ret));
        initializedSuccessfully = false;
    }

    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf((char*)ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid), "%s%02X%02X",
             CONFIG_VE_AP_SSID_PREFIX, mac[4], mac[5]);
    ESP_LOGI(TAG, "Device MAC: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "Init netif + event loop");
    ESP_ERROR_CHECK(esp_netif_init());
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
        initializedSuccessfully = false;
    }

    ESP_LOGI(TAG, "Init WiFi driver");
    ESP_ERROR_CHECK(wifi_init_once());

    ap_cfg_fixup(&ap_cfg);

    ESP_LOGI(TAG, "Starting WiFi... mode=%d", (int)VgConfig.network_mode);
    ESP_ERROR_CHECK(wifi_apply_mode(VgConfig.network_mode, &sta_cfg, &ap_cfg));

    ESP_LOGI(TAG, "Registering HTTP server event handlers");
    ESP_ERROR_CHECK(http_server_register_event_handlers());

    ESP_LOGI(TAG, "Starting HTTP server");
    ret = http_server_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "http_server_start failed: %s", esp_err_to_name(ret));
        initializedSuccessfully = false;
        return ret;
    }
    ESP_LOGI(TAG, "HTTP server started successfully");

    if (VgConfig.is_master) {
        ESP_LOGI(TAG, "Starting HTTP client task for master mode");
        ret = init_master_mode();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "init_master_mode failed: %s", esp_err_to_name(ret));
            initializedSuccessfully = false;
            return ret;
        }
        ESP_LOGI(TAG, "HTTP client task for master mode started successfully");
    }

#if CONFIG_VE_ENABLE_I2C
    ESP_LOGI(TAG, "I2C is enabled in config; initializing bus");
    ret = i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_init failed: %s", esp_err_to_name(ret));
        initializedSuccessfully = false;

    } else {
        ESP_LOGI(TAG, "I2C bus initialized successfully");
    }
#else
    ESP_LOGI(TAG, "I2C support is disabled in config");
#endif

    if (!initializedSuccessfully) {
        ESP_LOGE(TAG, "Vigilant initialization failed due to previous errors");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Vigilant initialized successfully!");
    ESP_LOGI(TAG, "This node unique name is: %s",
             VgConfig.unique_component_name);
    s_cfg = VgConfig;

    // Set info status once

    return ESP_OK;
}

esp_err_t vigilant_get_info(VigilantInfo* info) {
    if (!info) return ESP_ERR_INVALID_ARG;

    memset(info, 0, sizeof(*info));
    memcpy(info->unique_component_name, s_cfg.unique_component_name,
           sizeof(info->unique_component_name));
    info->network_mode = s_cfg.network_mode;

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(info->mac, sizeof(info->mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    snprintf(info->ap_ssid, sizeof(info->ap_ssid), "%s",
             (const char*)ap_cfg.ap.ssid);
    snprintf(info->sta_ssid, sizeof(info->sta_ssid), "%s",
             (const char*)sta_cfg.sta.ssid);

    esp_netif_ip_info_t ip_sta = {0};
    if (s_netif_sta && esp_netif_get_ip_info(s_netif_sta, &ip_sta) == ESP_OK) {
        inet_ntoa_r(ip_sta.ip, info->ip_sta, sizeof(info->ip_sta));
    } else {
        strcpy(info->ip_sta, "0.0.0.0");
    }

    esp_netif_ip_info_t ip_ap = {0};
    if (s_netif_ap && esp_netif_get_ip_info(s_netif_ap, &ip_ap) == ESP_OK) {
        inet_ntoa_r(ip_ap.ip, info->ip_ap, sizeof(info->ip_ap));
    } else {
        strcpy(info->ip_ap, "0.0.0.0");
    }

    return ESP_OK;
}

static size_t bounded_u8_strlen(const uint8_t* s, size_t max_len) {
    size_t len = 0;
    while (len < max_len && s[len] != 0) {
        len++;
    }
    return len;
}

static void copy_ssid(char* dst, size_t dst_size, const uint8_t* src,
                      size_t src_len) {
    if (!dst || dst_size == 0) {
        return;
    }

    dst[0] = '\0';

    if (!src) {
        return;
    }

    if (src_len >= dst_size) {
        src_len = dst_size - 1;
    }

    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
}

static void set_ipv4_or_zero(char* dst, size_t dst_size, esp_netif_t* netif) {
    if (!dst || dst_size == 0) {
        return;
    }

    snprintf(dst, dst_size, "0.0.0.0");

    if (!netif) {
        return;
    }

    esp_netif_ip_info_t ip_info = {0};

    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(dst, dst_size, IPSTR, IP2STR(&ip_info.ip));
    }
}

static NW_MODE map_wifi_mode(wifi_mode_t mode) {
    switch (mode) {
        case WIFI_MODE_AP:
            return NW_MODE_AP;
        case WIFI_MODE_STA:
            return NW_MODE_STA;
        case WIFI_MODE_APSTA:
            return NW_MODE_APSTA;
        default:
            return NW_MODE_STA;
    }
}

esp_err_t vigilant_get_wifiinfo(VigilantWiFiInfo* info) {
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));

    snprintf(info->mac, sizeof(info->mac), "00:00:00:00:00:00");
    snprintf(info->ip_sta, sizeof(info->ip_sta), "0.0.0.0");
    snprintf(info->ip_ap, sizeof(info->ip_ap), "0.0.0.0");

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        return err;
    }

    info->network_mode = map_wifi_mode(mode);

    /*
     * One displayed MAC only:
     * - AP-only: use AP MAC
     * - STA/APSTA/NULL: use STA MAC as primary device MAC
     */
    uint8_t mac[6] = {0};
    wifi_interface_t mac_if = (mode == WIFI_MODE_AP) ? WIFI_IF_AP : WIFI_IF_STA;

    err = esp_wifi_get_mac(mac_if, mac);
    if (err != ESP_OK && mac_if != WIFI_IF_AP) {
        err = esp_wifi_get_mac(WIFI_IF_AP, mac);
    }

    if (err == ESP_OK) {
        snprintf(info->mac, sizeof(info->mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    /*
     * Configured STA SSID.
     * esp_wifi_get_config() fails if that interface is not enabled,
     * so ignore the error here and leave the field empty.
     */
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        wifi_config_t sta_cfg = {0};

        if (esp_wifi_get_config(WIFI_IF_STA, &sta_cfg) == ESP_OK) {
            size_t sta_ssid_len =
                bounded_u8_strlen(sta_cfg.sta.ssid, sizeof(sta_cfg.sta.ssid));

            copy_ssid(info->sta_ssid, sizeof(info->sta_ssid), sta_cfg.sta.ssid,
                      sta_ssid_len);
        }
    }

    /*
     * Configured SoftAP SSID.
     */
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        wifi_config_t ap_cfg = {0};

        if (esp_wifi_get_config(WIFI_IF_AP, &ap_cfg) == ESP_OK) {
            size_t ap_ssid_len = ap_cfg.ap.ssid_len;

            if (ap_ssid_len == 0) {
                ap_ssid_len =
                    bounded_u8_strlen(ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid));
            }

            copy_ssid(info->ap_ssid, sizeof(info->ap_ssid), ap_cfg.ap.ssid,
                      ap_ssid_len);
        }
    }

    /*
     * Current IPv4 addresses.
     * This assumes default Wi-Fi netifs were created with:
     *   esp_netif_create_default_wifi_sta()
     *   esp_netif_create_default_wifi_ap()
     */
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

    set_ipv4_or_zero(info->ip_sta, sizeof(info->ip_sta), sta_netif);
    set_ipv4_or_zero(info->ip_ap, sizeof(info->ip_ap), ap_netif);

    /*
     * Currently associated SoftAP clients.
     */
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        wifi_sta_list_t sta_list = {0};

        if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
            wifi_sta_mac_ip_list_t ip_list = {0};
            bool have_ip_list = (esp_wifi_ap_get_sta_list_with_ip(
                                     &sta_list, &ip_list) == ESP_OK);
            uint8_t connected_count = (uint8_t)sta_list.num;

            if (connected_count > VIGILANT_WIFI_MAX_CONNECTED_DEVICES) {
                connected_count = VIGILANT_WIFI_MAX_CONNECTED_DEVICES;
            }

            info->connected_devices_count = connected_count;

            for (uint8_t i = 0; i < connected_count; ++i) {
                VigilantWifiDevice* device = &info->connected_devices[i];
                const uint8_t* device_mac = sta_list.sta[i].mac;
                VigilantWifiIdentityCacheEntry cached_identity = {0};

                format_mac_address(device->mac, sizeof(device->mac),
                                   device_mac);
                copy_string(device->name, sizeof(device->name), device->mac);
                device->identity = VIGILANT_WIFI_DEVICE_IDENTITY_UNKNOWN;
                device->is_vigilant_device = false;

                if (wifi_identity_cache_get(device->mac, &cached_identity)) {
                    device->identity = cached_identity.identity;
                    device->is_vigilant_device =
                        cached_identity.identity ==
                        VIGILANT_WIFI_DEVICE_IDENTITY_VIGILANT;

                    if (cached_identity.name[0] != '\0') {
                        copy_string(device->name, sizeof(device->name),
                                    cached_identity.name);
                    }
                }

                if (have_ip_list && i < ip_list.num) {
                    device->address = ip_list.sta[i].ip.addr;
                }
            }
        }
    }

    return ESP_OK;
}

esp_err_t vigilant_get_i2cinfo(VigilantI2cInfo* info) {
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));

#if CONFIG_VE_ENABLE_I2C
    info->enabled = true;
    info->sda_io = CONFIG_VE_I2C_SDA_IO;
    info->scl_io = CONFIG_VE_I2C_SCL_IO;
    info->frequency_hz = CONFIG_VE_I2C_FREQ_HZ;
    info->added_device_count = (uint8_t)s_i2c_device_count;
    memcpy(info->added_devices, s_i2c_devices,
           sizeof(s_i2c_devices[0]) * s_i2c_device_count);

    size_t detected_count = 0;
    esp_err_t detected_err = i2c_get_detected_devices(
        info->detected_devices,
        sizeof(info->detected_devices) / sizeof(info->detected_devices[0]),
        &detected_count);
    if (detected_err != ESP_OK) {
        return detected_err;
    }

    info->detected_device_count = (uint8_t)detected_count;
#else
    info->enabled = false;
#endif

    return ESP_OK;
}

esp_err_t vigilant_i2c_add_device(VigilantI2CDevice* device) {
#if CONFIG_VE_ENABLE_I2C
    esp_err_t err = i2c_add_device(device);
    if (err == ESP_OK) {
        sync_registered_i2c_device(device);
    }
    return err;
#else
    (void)device;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t vigilant_i2c_remove_device(VigilantI2CDevice* device) {
#if CONFIG_VE_ENABLE_I2C
    esp_err_t err = i2c_remove_device(device);
    if (err == ESP_OK) {
        remove_registered_i2c_device(device);
    }
    return err;
#else
    (void)device;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t vigilant_i2c_set_reg8(VigilantI2CDevice* device, uint8_t reg,
                                uint8_t value) {
#if CONFIG_VE_ENABLE_I2C
    return i2c_set_reg8(device, reg, value);
#else
    (void)device;
    (void)reg;
    (void)value;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t vigilant_i2c_read_reg8(VigilantI2CDevice* device, uint8_t reg,
                                 uint8_t* value) {
#if CONFIG_VE_ENABLE_I2C
    return i2c_read_reg8(device, reg, value);
#else
    (void)device;
    (void)reg;
    (void)value;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t vigilant_i2c_whoami_check(VigilantI2CDevice* device) {
#if CONFIG_VE_ENABLE_I2C
    return i2c_whoami_check(device);
#else
    (void)device;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
