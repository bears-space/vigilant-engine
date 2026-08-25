#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "status_led.h"

static const char* TAG = "ve_recovery";

typedef enum {
    RECOVERY_NETWORK_MODE_AP = 0,
    RECOVERY_NETWORK_MODE_STA,
    RECOVERY_NETWORK_MODE_APSTA,
} recovery_network_mode_t;

#ifdef VE_RECOVERY_USE_PARENT_CONFIG
#include "ve_recovery_config.h"
#else
#if CONFIG_VE_RECOVERY_NETWORK_MODE_APSTA
#define VE_RECOVERY_NETWORK_MODE RECOVERY_NETWORK_MODE_APSTA
#elif CONFIG_VE_RECOVERY_NETWORK_MODE_STA
#define VE_RECOVERY_NETWORK_MODE RECOVERY_NETWORK_MODE_STA
#else
#define VE_RECOVERY_NETWORK_MODE RECOVERY_NETWORK_MODE_AP
#endif

#ifndef CONFIG_VE_RECOVERY_STA_SSID
#define CONFIG_VE_RECOVERY_STA_SSID ""
#endif
#ifndef CONFIG_VE_RECOVERY_STA_PASSWORD
#define CONFIG_VE_RECOVERY_STA_PASSWORD ""
#endif
#ifndef CONFIG_VE_RECOVERY_AP_SSID_PREFIX
#define CONFIG_VE_RECOVERY_AP_SSID_PREFIX ""
#endif
#ifndef CONFIG_VE_RECOVERY_AP_PASSWORD
#define CONFIG_VE_RECOVERY_AP_PASSWORD ""
#endif

#define VE_RECOVERY_STA_SSID CONFIG_VE_RECOVERY_STA_SSID
#define VE_RECOVERY_STA_PASSWORD CONFIG_VE_RECOVERY_STA_PASSWORD
#define VE_RECOVERY_AP_SSID_PREFIX CONFIG_VE_RECOVERY_AP_SSID_PREFIX
#define VE_RECOVERY_AP_PASSWORD CONFIG_VE_RECOVERY_AP_PASSWORD
#define VE_RECOVERY_AP_CHANNEL CONFIG_VE_VE_RECOVERY_AP_CHANNEL
#define VE_RECOVERY_MAX_CONN CONFIG_VE_VE_RECOVERY_MAX_CONN
#define VE_RECOVERY_CONNECTION_TIMEOUT_SECONDS \
    CONFIG_VE_VE_RECOVERY_CONNECTION_TIMEOUT_SECONDS
#endif

// ---- OTA ----
#define OTA_BUF_SIZE 2048

extern const unsigned char index_html_start[] asm("_binary_index_html_start");
extern const unsigned char index_html_end[] asm("_binary_index_html_end");

static volatile bool s_sta_has_ip = false;

static bool recovery_mode_has_ap(void) {
    return VE_RECOVERY_NETWORK_MODE == RECOVERY_NETWORK_MODE_AP ||
           VE_RECOVERY_NETWORK_MODE == RECOVERY_NETWORK_MODE_APSTA;
}

static bool recovery_mode_has_sta(void) {
    return VE_RECOVERY_NETWORK_MODE == RECOVERY_NETWORK_MODE_STA ||
           VE_RECOVERY_NETWORK_MODE == RECOVERY_NETWORK_MODE_APSTA;
}

static const char* recovery_mode_name(void) {
    switch (VE_RECOVERY_NETWORK_MODE) {
        case RECOVERY_NETWORK_MODE_AP:
            return "AP";
        case RECOVERY_NETWORK_MODE_STA:
            return "STA";
        case RECOVERY_NETWORK_MODE_APSTA:
            return "AP+STA";
        default:
            return "invalid";
    }
}

static esp_err_t index_get_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    return httpd_resp_send(req, (const char*)index_html_start,
                           index_html_end - index_html_start);
}

static const esp_partition_t* find_ota0_partition(void) {
    // Hard-pin to ota_0 as you requested
    const esp_partition_t* p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    return p;
}

static esp_err_t boot_ota0_partition(void) {
    const esp_partition_t* ota0 = find_ota0_partition();
    if (!ota0) {
        ESP_LOGE(TAG, "ota_0 partition not found");
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = esp_ota_set_boot_partition(ota0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Boot partition set to ota_0. Rebooting…");
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
    return ESP_OK;  // should never reach here
}

static esp_err_t boot_post_handler(httpd_req_t* req) {
    esp_err_t boot_err = boot_ota0_partition();
    if (boot_err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Failed to set boot partition");
        return boot_err;
    }
    return ESP_OK;  // should never reach here
}

static esp_err_t ota_post_handler(httpd_req_t* req) {
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    const esp_partition_t* update_partition = find_ota0_partition();
    if (!update_partition) {
        ESP_LOGE(TAG, "ota_0 partition not found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "ota_0 not found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA target: label=%s subtype=0x%02x offset=0x%lx size=0x%lx",
             update_partition->label, update_partition->subtype,
             (unsigned long)update_partition->address,
             (unsigned long)update_partition->size);

    if (req->content_len > update_partition->size) {
        ESP_LOGE(TAG, "Image too large: %d > 0x%lx", req->content_len,
                 (unsigned long)update_partition->size);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Image too large for ota_0");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err =
        esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "esp_ota_begin failed");
        return ESP_FAIL;
    }

    uint8_t* buf = (uint8_t*)malloc(OTA_BUF_SIZE);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "malloc failed");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    int written_total = 0;

    while (remaining > 0) {
        int to_read = remaining > OTA_BUF_SIZE ? OTA_BUF_SIZE : remaining;
        int r = httpd_req_recv(req, (char*)buf, to_read);

        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;  // retry
        }
        if (r < 0) {
            ESP_LOGE(TAG, "httpd_req_recv error: %d", r);
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "recv failed");
            return ESP_FAIL;
        }
        if (r == 0) {
            ESP_LOGE(TAG, "client closed connection early");
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "connection closed");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "write failed");
            return ESP_FAIL;
        }

        remaining -= r;
        written_total += r;
    }

    free(buf);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "ota_end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
                 esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "set_boot_partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA OK: wrote %d bytes. Rebooting to ota_0…", written_total);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK. Rebooting to ota_0...\n");

    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
    return ESP_OK;
}

static httpd_handle_t start_http_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return NULL;
    }

    httpd_uri_t index_uri = {.uri = "/",
                             .method = HTTP_GET,
                             .handler = index_get_handler,
                             .user_ctx = NULL};

    httpd_uri_t update_uri = {.uri = "/update",
                              .method = HTTP_POST,
                              .handler = ota_post_handler,
                              .user_ctx = NULL};

    httpd_uri_t boot_uri = {.uri = "/boot",
                            .method = HTTP_POST,
                            .handler = boot_post_handler,
                            .user_ctx = NULL};

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &update_uri);
    httpd_register_uri_handler(server, &boot_uri);

    ESP_LOGI(TAG, "HTTP server started");
    return server;
}

static void copy_wifi_config_value(uint8_t* destination,
                                   size_t destination_size, const char* value,
                                   const char* name) {
    size_t value_length = strlen(value);
    if (value_length >= destination_size) {
        ESP_LOGE(TAG, "%s is too long (maximum %u characters)", name,
                 (unsigned int)(destination_size - 1));
        abort();
    }

    memcpy(destination, value, value_length + 1);
}

static size_t copy_wifi_ssid(uint8_t* destination, size_t destination_size,
                             const char* value, const char* name) {
    size_t value_length = strlen(value);
    if (value_length == 0 || value_length > destination_size) {
        ESP_LOGE(TAG, "%s must be between 1 and %u characters", name,
                 (unsigned int)destination_size);
        abort();
    }

    memcpy(destination, value, value_length);
    return value_length;
}

static void validate_wifi_password(const char* password, const char* name) {
    size_t password_length = strlen(password);
    if (password_length != 0 && password_length < 8) {
        ESP_LOGE(TAG, "%s must be empty or between 8 and 63 characters", name);
        abort();
    }
    if (password_length > 63) {
        ESP_LOGE(TAG, "%s must be empty or between 8 and 63 characters", name);
        abort();
    }
}

static void recovery_wifi_event(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event =
            (wifi_event_sta_disconnected_t*)event_data;
        s_sta_has_ip = false;
        ESP_LOGW(TAG, "Recovery STA disconnected, reason=%d; reconnecting",
                 event->reason);
        esp_err_t connect_err = esp_wifi_connect();
        if (connect_err != ESP_OK) {
            ESP_LOGW(TAG, "Recovery STA reconnect failed: %s",
                     esp_err_to_name(connect_err));
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        s_sta_has_ip = true;
        ESP_LOGI(TAG, "Recovery STA got IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init_recovery(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (recovery_mode_has_sta() &&
        esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");
        abort();
    }
    if (recovery_mode_has_ap() && esp_netif_create_default_wifi_ap() == NULL) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_ap failed");
        abort();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    if (recovery_mode_has_sta()) {
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                                   WIFI_EVENT_STA_DISCONNECTED,
                                                   recovery_wifi_event, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, recovery_wifi_event, NULL));
    }

    wifi_config_t ap_cfg = {0};
    wifi_config_t sta_cfg = {0};

    if (recovery_mode_has_ap()) {
        validate_wifi_password(VE_RECOVERY_AP_PASSWORD, "Recovery AP password");

        uint8_t mac[6];
        char ap_ssid[sizeof(ap_cfg.ap.ssid) + 1];
        ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
        int ssid_length = snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X",
                                   VE_RECOVERY_AP_SSID_PREFIX, mac[4], mac[5]);
        if (ssid_length < 0 || ssid_length >= (int)sizeof(ap_ssid)) {
            ESP_LOGE(TAG,
                     "Recovery AP SSID prefix is too long (the generated SSID "
                     "must be at most 32 characters)");
            abort();
        }

        ap_cfg.ap.ssid_len =
            copy_wifi_ssid(ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid), ap_ssid,
                           "Recovery AP SSID");
        ap_cfg.ap.channel = VE_RECOVERY_AP_CHANNEL;
        ap_cfg.ap.max_connection = VE_RECOVERY_MAX_CONN;
        copy_wifi_config_value(ap_cfg.ap.password, sizeof(ap_cfg.ap.password),
                               VE_RECOVERY_AP_PASSWORD, "Recovery AP password");
        ap_cfg.ap.authmode = strlen(VE_RECOVERY_AP_PASSWORD) == 0
                                 ? WIFI_AUTH_OPEN
                                 : WIFI_AUTH_WPA2_PSK;

        ESP_LOGI(TAG,
                 "Device MAC: %02X:%02X:%02X:%02X:%02X:%02X; recovery AP "
                 "SSID='%s' (%s)",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ap_ssid,
                 ap_cfg.ap.authmode == WIFI_AUTH_OPEN ? "open" : "secured");
    }

    if (recovery_mode_has_sta()) {
        validate_wifi_password(VE_RECOVERY_STA_PASSWORD,
                               "Recovery STA password");
        copy_wifi_ssid(sta_cfg.sta.ssid, sizeof(sta_cfg.sta.ssid),
                       VE_RECOVERY_STA_SSID, "Recovery STA SSID");
        copy_wifi_config_value(
            sta_cfg.sta.password, sizeof(sta_cfg.sta.password),
            VE_RECOVERY_STA_PASSWORD, "Recovery STA password");
        sta_cfg.sta.threshold.authmode = strlen(VE_RECOVERY_STA_PASSWORD) == 0
                                             ? WIFI_AUTH_OPEN
                                             : WIFI_AUTH_WPA2_PSK;
    }

    wifi_mode_t wifi_mode = WIFI_MODE_AP;
    if (VE_RECOVERY_NETWORK_MODE == RECOVERY_NETWORK_MODE_STA) {
        wifi_mode = WIFI_MODE_STA;
    } else if (VE_RECOVERY_NETWORK_MODE == RECOVERY_NETWORK_MODE_APSTA) {
        wifi_mode = WIFI_MODE_APSTA;
    }

    ESP_LOGI(TAG, "Starting recovery WiFi in %s mode", recovery_mode_name());
    ESP_ERROR_CHECK(esp_wifi_set_mode(wifi_mode));
    if (recovery_mode_has_ap()) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    }
    if (recovery_mode_has_sta()) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    if (recovery_mode_has_sta()) {
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
        ESP_LOGI(TAG, "Connecting recovery STA to SSID='%s'",
                 VE_RECOVERY_STA_SSID);
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
}

static bool recovery_network_ready(void) {
    if (recovery_mode_has_sta() && s_sta_has_ip) {
        return true;
    }

    if (recovery_mode_has_ap()) {
        wifi_sta_list_t sta_list;
        esp_err_t list_err = esp_wifi_ap_get_sta_list(&sta_list);
        if (list_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get recovery AP station list: %s",
                     esp_err_to_name(list_err));
            return false;
        }
        if (sta_list.num > 0) {
            ESP_LOGI(TAG, "Device connected to recovery AP: %d device(s)",
                     sta_list.num);
            return true;
        }
    }

    return false;
}

void app_main(void) {
    /*

    IMPORTANT:
    A note to auto booting when we implement boot flags to determine if we are
    in flight or not, etc.

    We might want to skip the recovery mode if we are in flight or if we have a
    valid boot flag set. This ensures we dont wait a long time for a device to
    connect, when we know no device will connect in flight. Furthermore, this
    allows for quickly recovering the device in flight.

    */

    esp_err_t led_err = init_led();
    if (led_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init status LED: %s",
                 esp_err_to_name(led_err));
    }

    // NVS required for WiFi on many setups
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs);
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running from: label=%s subtype=0x%02x offset=0x%lx",
             running->label, running->subtype, (unsigned long)running->address);

    wifi_init_recovery();
    start_http_server();

    bool network_ready = false;
    for (size_t i = 0;
         i < VE_RECOVERY_CONNECTION_TIMEOUT_SECONDS && !network_ready; i++) {
        ESP_LOGI(TAG, "Waiting for recovery network... (%u/%u)",
                 (unsigned int)(i + 1), VE_RECOVERY_CONNECTION_TIMEOUT_SECONDS);
        network_ready = recovery_network_ready();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (network_ready) {
        ESP_LOGI(TAG, "Recovery network is ready");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "Recovery network did not become ready. Booting ota_0...");
    esp_err_t boot_err = boot_ota0_partition();
    if (boot_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to boot ota_0 partition: %s",
                 esp_err_to_name(boot_err));
        abort();
    }
}
