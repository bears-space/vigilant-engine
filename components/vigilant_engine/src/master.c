#include "master.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "cJSON.h"
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
#define VE_MASTER_HTTP_RESPONSE_BUFFER_SIZE 1024
#define VE_MASTER_HTTP_PORT 80

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
    int status_code;
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

static esp_err_t socket_send_all(int sock, const char* data, size_t len) {
    size_t sent = 0;

    while (sent < len) {
        ssize_t written = send(sock, data + sent, len - sent, 0);
        if (written < 0) {
            return ESP_FAIL;
        }

        if (written == 0) {
            return ESP_ERR_TIMEOUT;
        }

        sent += (size_t)written;
    }

    return ESP_OK;
}

static int parse_http_status_code(const char* response) {
    if (!response || strncmp(response, "HTTP/", 5) != 0) {
        return 0;
    }

    const char* status_start = strchr(response, ' ');
    if (!status_start) {
        return 0;
    }

    char* status_end = NULL;
    long status = strtol(status_start + 1, &status_end, 10);
    if (status_end == status_start + 1 || status < 100 || status > 999) {
        return 0;
    }

    return (int)status;
}

static const char* find_http_body_start(const char* response,
                                        size_t* headers_len) {
    const char* crlf_end = strstr(response, "\r\n\r\n");
    if (crlf_end) {
        if (headers_len) {
            *headers_len = (size_t)(crlf_end - response);
        }
        return crlf_end + 4;
    }

    const char* lf_end = strstr(response, "\n\n");
    if (lf_end) {
        if (headers_len) {
            *headers_len = (size_t)(lf_end - response);
        }
        return lf_end + 2;
    }

    return NULL;
}

static bool header_value_matches_magic(const char* value, size_t value_len) {
    while (value_len > 0 && (*value == ' ' || *value == '\t')) {
        ++value;
        --value_len;
    }

    while (value_len > 0 &&
           (value[value_len - 1] == ' ' || value[value_len - 1] == '\t' ||
            value[value_len - 1] == '\r')) {
        --value_len;
    }

    size_t magic_len = strlen(VIGILANT_DEVICE_MAGIC);
    return value_len == magic_len &&
           strncmp(value, VIGILANT_DEVICE_MAGIC, magic_len) == 0;
}

static bool response_has_magic_header(const char* headers, size_t headers_len) {
    const char* cursor = headers;
    const char* end = headers + headers_len;
    size_t magic_header_len = strlen(VIGILANT_DEVICE_MAGIC_HEADER);

    while (cursor < end) {
        const char* line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        if (!line_end) {
            line_end = end;
        }

        const char* colon = memchr(cursor, ':', (size_t)(line_end - cursor));
        if (colon) {
            size_t key_len = (size_t)(colon - cursor);
            if (key_len == magic_header_len &&
                strncasecmp(cursor, VIGILANT_DEVICE_MAGIC_HEADER, key_len) ==
                    0 &&
                header_value_matches_magic(colon + 1,
                                           (size_t)(line_end - colon - 1))) {
                return true;
            }
        }

        cursor = line_end < end ? line_end + 1 : end;
    }

    return false;
}

static esp_err_t read_probe_response(int sock, char* response,
                                     size_t response_size, size_t* response_len,
                                     bool* response_truncated) {
    *response_len = 0;
    *response_truncated = false;

    while (*response_len < response_size - 1) {
        ssize_t received = recv(sock, response + *response_len,
                                response_size - *response_len - 1, 0);

        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            return ESP_FAIL;
        }

        if (received == 0) {
            break;
        }

        *response_len += (size_t)received;
        response[*response_len] = '\0';
    }

    if (*response_len == response_size - 1) {
        *response_truncated = true;
    }

    return *response_len > 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t perform_probe_request(const VigilantWifiDevice* device,
                                       VigilantProbeContext* probe_ctx) {
    esp_ip4_addr_t ip = {.addr = device->address};
    char host[16];
    snprintf(host, sizeof(host), IPSTR, IP2STR(&ip));

    char request[160];
    int request_len = snprintf(request, sizeof(request),
                               "GET /info HTTP/1.0\r\n"
                               "Host: %s\r\n"
                               "User-Agent: vigilant-engine-master\r\n"
                               "Accept: application/json\r\n"
                               "Connection: close\r\n\r\n",
                               host);
    if (request_len < 0 || request_len >= (int)sizeof(request)) {
        return ESP_ERR_INVALID_SIZE;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        return ESP_FAIL;
    }

    struct timeval timeout = {
        .tv_sec = VE_MASTER_PROBE_TIMEOUT_MS / 1000,
        .tv_usec = (VE_MASTER_PROBE_TIMEOUT_MS % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in dest_addr = {0};
    dest_addr.sin_addr.s_addr = device->address;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(VE_MASTER_HTTP_PORT);

    esp_err_t result = ESP_OK;
    if (connect(sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) != 0) {
        result = ESP_FAIL;
        goto cleanup;
    }

    result = socket_send_all(sock, request, (size_t)request_len);
    if (result != ESP_OK) {
        goto cleanup;
    }

    char response[VE_MASTER_HTTP_RESPONSE_BUFFER_SIZE] = {0};
    size_t response_len = 0;
    bool response_truncated = false;
    result = read_probe_response(sock, response, sizeof(response),
                                 &response_len, &response_truncated);
    if (result != ESP_OK) {
        goto cleanup;
    }

    size_t headers_len = 0;
    const char* body_start = find_http_body_start(response, &headers_len);
    if (!body_start) {
        result = ESP_FAIL;
        goto cleanup;
    }

    probe_ctx->status_code = parse_http_status_code(response);
    probe_ctx->has_magic_header =
        response_has_magic_header(response, headers_len);

    size_t body_offset = (size_t)(body_start - response);
    size_t body_available =
        response_len > body_offset ? response_len - body_offset : 0;
    size_t body_to_copy = body_available;
    if (body_to_copy >= sizeof(probe_ctx->body)) {
        body_to_copy = sizeof(probe_ctx->body) - 1;
        probe_ctx->body_truncated = true;
    }

    if (response_truncated && body_available >= sizeof(probe_ctx->body) - 1) {
        probe_ctx->body_truncated = true;
    }

    memcpy(probe_ctx->body, body_start, body_to_copy);
    probe_ctx->body_len = body_to_copy;
    probe_ctx->body[probe_ctx->body_len] = '\0';

cleanup:
    close(sock);
    return result;
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

    VigilantProbeContext probe_ctx = {0};
    esp_err_t err = perform_probe_request(device, &probe_ctx);
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

    if (probe_ctx.status_code == 200 &&
        (probe_ctx.has_magic_header || body_has_magic)) {
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
