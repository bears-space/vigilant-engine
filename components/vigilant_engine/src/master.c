#include "master.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "vigilant.h"

static const char* TAG = "ve-master";

#define VE_MASTER_POLL_INTERVAL_MS 10000
#define VE_MASTER_PROBE_TIMEOUT_MS 1200
#define VE_MASTER_OTHER_RETRY_INTERVAL_MS 60000
#define VE_MASTER_RESPONSE_BUFFER_SIZE 512

typedef enum {
    VE_PROBE_RESULT_VIGILANT,
    VE_PROBE_RESULT_OTHER,
    VE_PROBE_RESULT_TRANSIENT,
} VigilantProbeResult;

typedef struct {
    char body[VE_MASTER_RESPONSE_BUFFER_SIZE];
    size_t body_len;
    bool body_truncated;
    bool has_magic_header;
} VigilantProbeContext;

typedef struct {
    bool in_use;
    char mac[18];
    TickType_t last_probe_tick;
} VigilantProbeRecord;

static VigilantProbeRecord
    s_probe_records[VIGILANT_WIFI_MAX_CONNECTED_DEVICES] = {0};
static TaskHandle_t s_master_task_handle = NULL;

static const char* identity_to_string(VigilantWifiDeviceIdentity identity) {
    switch (identity) {
        case VIGILANT_WIFI_DEVICE_IDENTITY_VIGILANT:
            return "vigilant";
        case VIGILANT_WIFI_DEVICE_IDENTITY_OTHER:
            return "other";
        case VIGILANT_WIFI_DEVICE_IDENTITY_UNKNOWN:
        default:
            return "unknown";
    }
}

static const char* device_mac_or_name(const VigilantWifiDevice* device) {
    if (!device) {
        return "";
    }

    return device->mac[0] != '\0' ? device->mac : device->name;
}

static int probe_record_find(const char* mac) {
    if (!mac || mac[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < VIGILANT_WIFI_MAX_CONNECTED_DEVICES; ++i) {
        if (s_probe_records[i].in_use &&
            strcasecmp(s_probe_records[i].mac, mac) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static VigilantProbeRecord* probe_record_for(const char* mac) {
    int index = probe_record_find(mac);
    if (index >= 0) {
        return &s_probe_records[index];
    }

    for (size_t i = 0; i < VIGILANT_WIFI_MAX_CONNECTED_DEVICES; ++i) {
        if (!s_probe_records[i].in_use) {
            index = (int)i;
            break;
        }
    }

    if (index < 0) {
        index = 0;
    }

    VigilantProbeRecord* record = &s_probe_records[index];
    memset(record, 0, sizeof(*record));
    record->in_use = true;
    snprintf(record->mac, sizeof(record->mac), "%s", mac);
    return record;
}

static void mark_probe_attempt(const VigilantWifiDevice* device) {
    const char* mac = device_mac_or_name(device);
    if (mac[0] == '\0') {
        return;
    }

    VigilantProbeRecord* record = probe_record_for(mac);
    record->last_probe_tick = xTaskGetTickCount();
}

static bool should_probe_device(const VigilantWifiDevice* device) {
    if (!device || device->address == 0) {
        return false;
    }

    if (device->identity == VIGILANT_WIFI_DEVICE_IDENTITY_UNKNOWN ||
        device->identity == VIGILANT_WIFI_DEVICE_IDENTITY_VIGILANT) {
        return true;
    }

    const char* mac = device_mac_or_name(device);
    int index = probe_record_find(mac);
    if (index < 0 || s_probe_records[index].last_probe_tick == 0) {
        return true;
    }

    TickType_t retry_interval =
        pdMS_TO_TICKS(VE_MASTER_OTHER_RETRY_INTERVAL_MS);
    TickType_t elapsed =
        xTaskGetTickCount() - s_probe_records[index].last_probe_tick;

    return elapsed >= retry_interval;
}

static bool probe_body_identifies_vigilant_device(const char* body, char* name,
                                                  size_t name_size) {
    if (!body || body[0] == '\0') {
        return false;
    }

    cJSON* root = cJSON_Parse(body);
    if (!root) {
        return false;
    }

    cJSON* magic = cJSON_GetObjectItem(root, "vigilant_magic");
    cJSON* is_vigilant = cJSON_GetObjectItem(root, "is_vigilant_device");
    bool valid = cJSON_IsString(magic) && magic->valuestring &&
                 strcmp(magic->valuestring, VIGILANT_DEVICE_MAGIC) == 0 &&
                 cJSON_IsTrue(is_vigilant);

    if (valid && name && name_size > 0) {
        cJSON* json_name = cJSON_GetObjectItem(root, "name");
        if (cJSON_IsString(json_name) && json_name->valuestring) {
            snprintf(name, name_size, "%s", json_name->valuestring);
        }
    }

    cJSON_Delete(root);
    return valid;
}

static esp_err_t probe_http_event_handler(esp_http_client_event_t* evt) {
    VigilantProbeContext* ctx = (VigilantProbeContext*)evt->user_data;
    if (!ctx) {
        return ESP_OK;
    }

    switch (evt->event_id) {
        case HTTP_EVENT_ON_HEADER:
            if (evt->header_key && evt->header_value &&
                strcasecmp(evt->header_key, VIGILANT_DEVICE_MAGIC_HEADER) ==
                    0 &&
                strcmp(evt->header_value, VIGILANT_DEVICE_MAGIC) == 0) {
                ctx->has_magic_header = true;
            }
            break;

        case HTTP_EVENT_ON_DATA:
            if (evt->data && evt->data_len > 0) {
                size_t available = sizeof(ctx->body) - ctx->body_len - 1;
                size_t to_copy = (size_t)evt->data_len;

                if (to_copy > available) {
                    to_copy = available;
                    ctx->body_truncated = true;
                }

                if (to_copy > 0) {
                    memcpy(ctx->body + ctx->body_len, evt->data, to_copy);
                    ctx->body_len += to_copy;
                    ctx->body[ctx->body_len] = '\0';
                }
            }
            break;

        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP probe error");
            break;

        default:
            break;
    }

    return ESP_OK;
}

static VigilantProbeResult probe_vigilant_device(
    const VigilantWifiDevice* device, char* discovered_name,
    size_t discovered_name_size) {
    if (discovered_name && discovered_name_size > 0) {
        discovered_name[0] = '\0';
    }

    if (!device || device->address == 0) {
        return VE_PROBE_RESULT_TRANSIENT;
    }

    esp_ip4_addr_t ip = {.addr = device->address};
    char url_buffer[64];
    snprintf(url_buffer, sizeof(url_buffer), "http://" IPSTR "/info",
             IP2STR(&ip));

    VigilantProbeContext probe_ctx = {0};
    esp_http_client_config_t config = {
        .url = url_buffer,
        .event_handler = probe_http_event_handler,
        .user_data = &probe_ctx,
        .timeout_ms = VE_MASTER_PROBE_TIMEOUT_MS,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return VE_PROBE_RESULT_TRANSIENT;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Probe failed for %s: %s", device_mac_or_name(device),
                 esp_err_to_name(err));
        return VE_PROBE_RESULT_TRANSIENT;
    }

    bool body_has_magic = probe_body_identifies_vigilant_device(
        probe_ctx.body, discovered_name, discovered_name_size);

    if (probe_ctx.body_truncated) {
        ESP_LOGD(TAG, "Probe body truncated for %s",
                 device_mac_or_name(device));
    }

    if (status == 200 && (probe_ctx.has_magic_header || body_has_magic)) {
        return VE_PROBE_RESULT_VIGILANT;
    }

    return VE_PROBE_RESULT_OTHER;
}

static void update_device_identity_from_probe(const VigilantWifiDevice* device,
                                              VigilantProbeResult result,
                                              const char* discovered_name) {
    VigilantWifiDeviceIdentity identity = VIGILANT_WIFI_DEVICE_IDENTITY_OTHER;

    if (result == VE_PROBE_RESULT_VIGILANT) {
        identity = VIGILANT_WIFI_DEVICE_IDENTITY_VIGILANT;
    } else if (result == VE_PROBE_RESULT_TRANSIENT &&
               device->identity == VIGILANT_WIFI_DEVICE_IDENTITY_VIGILANT) {
        ESP_LOGW(TAG, "Keeping %s as Vigilant after transient probe failure",
                 device_mac_or_name(device));
        return;
    }

    esp_err_t err = vigilant_update_wifi_device_identity(
        device_mac_or_name(device), device->address, identity, discovered_name);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update identity for %s: %s",
                 device_mac_or_name(device), esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Client %s classified as %s", device_mac_or_name(device),
             identity_to_string(identity));
}

static void http_fetch_task(void* arg) {
    (void)arg;

    const TickType_t interval = pdMS_TO_TICKS(VE_MASTER_POLL_INTERVAL_MS);
    TickType_t last_wake = xTaskGetTickCount();
    VigilantWiFiInfo wifi_info;

    while (true) {
        esp_err_t err = vigilant_get_wifiinfo(&wifi_info);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read WiFi info: %s", esp_err_to_name(err));
            vTaskDelayUntil(&last_wake, interval);
            continue;
        }

        if (wifi_info.connected_devices_count > 0) {
            ESP_LOGI(TAG, "Checking %d connected WiFi client(s)",
                     wifi_info.connected_devices_count);
        } else {
            ESP_LOGI(TAG, "No connected WiFi devices");
        }

        for (size_t i = 0; i < wifi_info.connected_devices_count; i++) {
            VigilantWifiDevice* device = &wifi_info.connected_devices[i];
            esp_ip4_addr_t ip = {.addr = device->address};

            ESP_LOGI(TAG, "Client %zu: %s at " IPSTR " identity=%s", i + 1,
                     device_mac_or_name(device), IP2STR(&ip),
                     identity_to_string(device->identity));

            if (!should_probe_device(device)) {
                ESP_LOGD(TAG, "Skipping non-Vigilant client %s",
                         device_mac_or_name(device));
                continue;
            }

            mark_probe_attempt(device);

            char discovered_name[32] = {0};
            VigilantProbeResult result = probe_vigilant_device(
                device, discovered_name, sizeof(discovered_name));
            update_device_identity_from_probe(device, result, discovered_name);
        }

        vTaskDelayUntil(&last_wake, interval);
    }
}

esp_err_t init_master_mode() {
    if (s_master_task_handle) {
        return ESP_OK;
    }

    BaseType_t created = xTaskCreate(http_fetch_task, "ve_master_http", 8192,
                                     NULL, 5, &s_master_task_handle);
    if (created != pdPASS) {
        s_master_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
