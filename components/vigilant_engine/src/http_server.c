// http_server.c

#include "http_server.h"

#include <errno.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_tls_crypto.h"
#include "nvs_flash.h"
#include "ota_http.h"
#include "sdkconfig.h"
#include "vigilant.h"
#include "websocket.h"

static const char* TAG = "http_server";

static httpd_handle_t s_server = NULL;  // unser globaler Server-Handle

#if defined(CONFIG_LWIP_MAX_SOCKETS) && CONFIG_LWIP_MAX_SOCKETS >= 13
#define VE_HTTPD_MAX_OPEN_SOCKETS \
    10  // Increase max open sockets if lwip socket amount is increased
#else
#define VE_HTTPD_MAX_OPEN_SOCKETS 7
#endif

#define VE_REMOTE_HTTP_TIMEOUT_MS 1500
#define VE_REMOTE_HTTP_RESPONSE_BUFFER_SIZE 1536
#define VE_REMOTE_HTTP_BODY_BUFFER_SIZE 1024
#define VE_REMOTE_HTTP_PORT 80

typedef struct {
    int status_code;
    char body[VE_REMOTE_HTTP_BODY_BUFFER_SIZE];
    size_t body_len;
    bool body_truncated;
} VeRemoteHttpResponse;

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static const char* wifi_identity_to_string(
    VigilantWifiDeviceIdentity identity) {
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

static void uri_decode(char* dest, size_t dest_size, const char* src,
                       size_t len) {
    if (!dest || dest_size == 0 || !src) {
        return;
    }

    size_t rd = 0;
    size_t wr = 0;
    while (rd < len && src[rd] != '\0' && wr + 1 < dest_size) {
        if (src[rd] == '%' && (rd + 2) < len && wr + 1 < dest_size) {
            int hi = hex_nibble(src[rd + 1]);
            int lo = hex_nibble(src[rd + 2]);
            if (hi >= 0 && lo >= 0) {
                dest[wr++] = (char)((hi << 4) | lo);
                rd += 3;
                continue;
            }
        }

        dest[wr++] = src[rd++];
    }

    dest[wr] = '\0';
}

static esp_err_t query_param_decoded(httpd_req_t* req, const char* key,
                                     char* dest, size_t dest_size) {
    if (!req || !key || !dest || dest_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    dest[0] = '\0';
    size_t query_len = httpd_req_get_url_query_len(req) + 1;
    if (query_len <= 1) {
        return ESP_ERR_NOT_FOUND;
    }

    char* query = malloc(query_len);
    ESP_RETURN_ON_FALSE(query, ESP_ERR_NO_MEM, TAG, "query alloc failed");

    esp_err_t err = httpd_req_get_url_query_str(req, query, query_len);
    if (err == ESP_OK) {
        char encoded[HTTP_QUERY_KEY_MAX_LEN] = {0};
        err = httpd_query_key_value(query, key, encoded, sizeof(encoded));
        if (err == ESP_OK) {
            uri_decode(dest, dest_size, encoded,
                       strnlen(encoded, sizeof(encoded)));
        }
    }

    free(query);
    return err;
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

static const char* find_http_body_start(const char* response) {
    const char* crlf_end = strstr(response, "\r\n\r\n");
    if (crlf_end) {
        return crlf_end + 4;
    }

    const char* lf_end = strstr(response, "\n\n");
    if (lf_end) {
        return lf_end + 2;
    }

    return NULL;
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

static esp_err_t read_remote_response(int sock, char* response,
                                      size_t response_size,
                                      size_t* response_len,
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

static esp_err_t remote_http_get(uint32_t address, const char* path,
                                 VeRemoteHttpResponse* out) {
    if (address == 0 || !path || path[0] != '/' || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    esp_ip4_addr_t ip = {.addr = address};
    char host[16];
    snprintf(host, sizeof(host), IPSTR, IP2STR(&ip));

    char request[192];
    int request_len = snprintf(request, sizeof(request),
                               "GET %s HTTP/1.0\r\n"
                               "Host: %s\r\n"
                               "User-Agent: vigilant-engine-master\r\n"
                               "Connection: close\r\n\r\n",
                               path, host);
    if (request_len < 0 || request_len >= (int)sizeof(request)) {
        return ESP_ERR_INVALID_SIZE;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        return ESP_FAIL;
    }

    struct timeval timeout = {
        .tv_sec = VE_REMOTE_HTTP_TIMEOUT_MS / 1000,
        .tv_usec = (VE_REMOTE_HTTP_TIMEOUT_MS % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in dest_addr = {0};
    dest_addr.sin_addr.s_addr = address;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(VE_REMOTE_HTTP_PORT);

    esp_err_t result = ESP_OK;
    if (connect(sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) != 0) {
        result = ESP_FAIL;
        goto cleanup;
    }

    result = socket_send_all(sock, request, (size_t)request_len);
    if (result != ESP_OK) {
        goto cleanup;
    }

    char response[VE_REMOTE_HTTP_RESPONSE_BUFFER_SIZE] = {0};
    size_t response_len = 0;
    bool response_truncated = false;
    result = read_remote_response(sock, response, sizeof(response),
                                  &response_len, &response_truncated);
    if (result != ESP_OK) {
        goto cleanup;
    }

    const char* body_start = find_http_body_start(response);
    if (!body_start) {
        result = ESP_FAIL;
        goto cleanup;
    }

    out->status_code = parse_http_status_code(response);

    size_t body_offset = (size_t)(body_start - response);
    size_t body_available =
        response_len > body_offset ? response_len - body_offset : 0;
    size_t body_to_copy = body_available;
    if (body_to_copy >= sizeof(out->body)) {
        body_to_copy = sizeof(out->body) - 1;
        out->body_truncated = true;
    }

    if (response_truncated && body_available >= sizeof(out->body) - 1) {
        out->body_truncated = true;
    }

    memcpy(out->body, body_start, body_to_copy);
    out->body_len = body_to_copy;
    out->body[out->body_len] = '\0';

cleanup:
    close(sock);
    return result;
}

static esp_err_t hello_get_handler(httpd_req_t* req) {
    char* buf;
    size_t buf_len;

    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buffer alloc failed");
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-2") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buffer alloc failed");
        if (httpd_req_get_hdr_value_str(req, "Test-Header-2", buf, buf_len) ==
            ESP_OK) {
            ESP_LOGI(TAG, "Found header => Test-Header-2: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-1") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buffer alloc failed");
        if (httpd_req_get_hdr_value_str(req, "Test-Header-1", buf, buf_len) ==
            ESP_OK) {
            ESP_LOGI(TAG, "Found header => Test-Header-1: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buffer alloc failed");
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[HTTP_QUERY_KEY_MAX_LEN],
                dec_param[HTTP_QUERY_KEY_MAX_LEN] = {0};
            if (httpd_query_key_value(buf, "query1", param, sizeof(param)) ==
                ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query1=%s", param);
                uri_decode(dec_param, sizeof(dec_param), param,
                           strnlen(param, HTTP_QUERY_KEY_MAX_LEN));
                ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
            }
            if (httpd_query_key_value(buf, "query3", param, sizeof(param)) ==
                ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query3=%s", param);
                uri_decode(dec_param, sizeof(dec_param), param,
                           strnlen(param, HTTP_QUERY_KEY_MAX_LEN));
                ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
            }
            if (httpd_query_key_value(buf, "query2", param, sizeof(param)) ==
                ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query2=%s", param);
                uri_decode(dec_param, sizeof(dec_param), param,
                           strnlen(param, HTTP_QUERY_KEY_MAX_LEN));
                ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
            }
        }
        free(buf);
    }

    httpd_resp_set_hdr(req, "Custom-Header-1", "Custom-Value-1");
    httpd_resp_set_hdr(req, "Custom-Header-2", "Custom-Value-2");

    const char* resp_str = (const char*)req->user_ctx;
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}

static const httpd_uri_t hello = {
    .uri = "/hello",
    .method = HTTP_GET,
    .handler = hello_get_handler,
    .user_ctx = "Hello World! This is the updated OTA version! :)"};

static esp_err_t echo_post_handler(httpd_req_t* req) {
    char buf[100];
    int ret, remaining = req->content_len;

    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <=
            0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }

        httpd_resp_send_chunk(req, buf, ret);
        remaining -= ret;

        ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        ESP_LOGI(TAG, "%.*s", ret, buf);
        ESP_LOGI(TAG, "====================================");
    }

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t echo = {.uri = "/echo",
                                 .method = HTTP_POST,
                                 .handler = echo_post_handler,
                                 .user_ctx = NULL};

static esp_err_t any_handler(httpd_req_t* req) {
    const char* resp_str = (const char*)req->user_ctx;
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t any = {.uri = "/any",
                                .method = HTTP_ANY,
                                .handler = any_handler,
                                .user_ctx = "Hello World!"};

static esp_err_t info_get_handler(httpd_req_t* req) {
    VigilantInfo info = {0};
    esp_err_t err = vigilant_get_info(&info);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Failed to fetch info");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, VIGILANT_DEVICE_MAGIC_HEADER,
                       VIGILANT_DEVICE_MAGIC);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char payload[512];
    int written = snprintf(
        payload, sizeof(payload),
        "{\"name\":\"%s\",\"is_vigilant_device\":true,\"vigilant_magic\":\"%"
        "s\",\"network_mode\":%d,\"mac\":\"%s\",\"ap_ssid\":\"%s\","
        "\"sta_ssid\":\"%s\",\"ip_sta\":\"%s\",\"ip_ap\":\"%s\"}",
        info.unique_component_name, VIGILANT_DEVICE_MAGIC,
        (int)info.network_mode, info.mac, info.ap_ssid, info.sta_ssid,
        info.ip_sta, info.ip_ap);

    if (written < 0 || written >= (int)sizeof(payload)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Info too large");
        return ESP_FAIL;
    }

    httpd_resp_send(req, payload, written);
    return ESP_OK;
}

static const httpd_uri_t info_uri = {
    .uri = "/info",
    .method = HTTP_GET,
    .handler = info_get_handler,
    .user_ctx = NULL,
};

static esp_err_t i2cinfo_get_handler(httpd_req_t* req) {
    VigilantI2cInfo info = {0};
    esp_err_t err = vigilant_get_i2cinfo(&info);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Failed to fetch i2c info");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    size_t payload_capacity =
        256  // Calculates a size for the json object, for malloc later
        + ((size_t)info.added_device_count *
           160)  // Added devices contain more information in the json than
                 // detected devices.
        + ((size_t)info.detected_device_count * 96);
    char* payload = malloc(payload_capacity);
    if (!payload) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "No memory for i2c info");
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;
    int written = snprintf(
        payload + offset, payload_capacity - offset,
        "{\"enabled\":%s,\"sda_io\":%u,\"scl_io\":%u,\"frequency_hz\":%" PRIu32
        ",\"added_device_count\":%u,\"detected_device_count\":%u,\"added_"
        "devices\":[",
        info.enabled ? "true" : "false", (unsigned int)info.sda_io,
        (unsigned int)info.scl_io, info.frequency_hz,
        (unsigned int)info.added_device_count,
        (unsigned int)info.detected_device_count);

    if (written < 0 || written >= (int)(payload_capacity - offset)) {
        free(payload);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Info too large");
        return ESP_FAIL;
    }
    offset += (size_t)written;

    for (uint8_t i = 0; i < info.added_device_count; ++i) {
        const VigilantI2CDevice* device = &info.added_devices[i];
        written = snprintf(
            payload + offset, payload_capacity - offset,
            "%s{\"name\":\"I2C Device "
            "0x%02X\",\"address\":%u,\"address_hex\":\"0x%02X\",\"whoami_reg\":"
            "%u,\"whoami_reg_hex\":\"0x%02X\",\"expected_whoami\":%u,"
            "\"expected_whoami_hex\":\"0x%02X\"}",
            i == 0 ? "" : ",", (unsigned int)device->address,
            (unsigned int)device->address, (unsigned int)device->address,
            (unsigned int)device->whoami_reg, (unsigned int)device->whoami_reg,
            (unsigned int)device->expected_whoami,
            (unsigned int)device->expected_whoami);

        if (written < 0 || written >= (int)(payload_capacity - offset)) {
            free(payload);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Info too large");
            return ESP_FAIL;
        }
        offset += (size_t)written;
    }

    written = snprintf(payload + offset, payload_capacity - offset,
                       "],\"detected_devices\":[");
    if (written < 0 || written >= (int)(payload_capacity - offset)) {
        free(payload);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Info too large");
        return ESP_FAIL;
    }
    offset += (size_t)written;

    for (uint8_t i = 0; i < info.detected_device_count; ++i) {
        uint8_t address = info.detected_devices[i];
        written =
            snprintf(payload + offset, payload_capacity - offset,
                     "%s{\"name\":\"Detected I2C Device "
                     "0x%02X\",\"address\":%u,\"address_hex\":\"0x%02X\"}",
                     i == 0 ? "" : ",", (unsigned int)address,
                     (unsigned int)address, (unsigned int)address);

        if (written < 0 || written >= (int)(payload_capacity - offset)) {
            free(payload);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Info too large");
            return ESP_FAIL;
        }
        offset += (size_t)written;
    }

    written = snprintf(payload + offset, payload_capacity - offset, "]}");
    if (written < 0 || written >= (int)(payload_capacity - offset)) {
        free(payload);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Info too large");
        return ESP_FAIL;
    }
    offset += (size_t)written;

    httpd_resp_send(req, payload, (ssize_t)offset);
    free(payload);
    return ESP_OK;
}

static esp_err_t wifiinfo_get_handler(httpd_req_t* req) {
    VigilantWiFiInfo info = {0};
    esp_err_t err = vigilant_get_wifiinfo(&info);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Failed to fetch wifi info");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    size_t payload_capacity =
        384 + ((size_t)info.connected_devices_count * 192);
    char* payload = calloc(1, payload_capacity);
    if (!payload) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Out of memory");
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;
    int written = snprintf(
        payload + offset, payload_capacity - offset,
        "{\"network_mode\":%d,\"mac\":\"%s\",\"ap_ssid\":\"%s\",\"sta_ssid\":"
        "\"%s\",\"ip_sta\":\"%s\",\"ip_ap\":\"%s\",\"connected_devices_count\":"
        "%u,\"connected_devices\":[",
        (int)info.network_mode, info.mac, info.ap_ssid, info.sta_ssid,
        info.ip_sta, info.ip_ap, (unsigned int)info.connected_devices_count);

    if (written < 0 || written >= (int)(payload_capacity - offset)) {
        free(payload);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Info too large");
        return ESP_FAIL;
    }
    offset += (size_t)written;

    for (uint8_t i = 0; i < info.connected_devices_count; ++i) {
        VigilantWifiDevice* device = &info.connected_devices[i];
        esp_ip4_addr_t ip = {.addr = device->address};

        written = snprintf(
            payload + offset, payload_capacity - offset,
            "%s{\"is_vigilant_device\":%s,\"identity\":\"%s\",\"name\":\"%"
            "s\",\"mac\":\"%s\",\"address\":%" PRIu32 ",\"address_ip\":\"" IPSTR
            "\"}",
            i == 0 ? "" : ",", device->is_vigilant_device ? "true" : "false",
            wifi_identity_to_string(device->identity), device->name,
            device->mac, device->address, IP2STR(&ip));

        if (written < 0 || written >= (int)(payload_capacity - offset)) {
            free(payload);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Info too large");
            return ESP_FAIL;
        }
        offset += (size_t)written;
    }

    written = snprintf(payload + offset, payload_capacity - offset, "]}");
    if (written < 0 || written >= (int)(payload_capacity - offset)) {
        free(payload);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Info too large");
        return ESP_FAIL;
    }
    offset += (size_t)written;

    httpd_resp_send(req, payload, (ssize_t)offset);
    free(payload);
    return ESP_OK;
}

static esp_err_t find_managed_wifi_device(httpd_req_t* req,
                                          VigilantWifiDevice* out_device) {
    char mac[HTTP_QUERY_KEY_MAX_LEN] = {0};
    esp_err_t err = query_param_decoded(req, "mac", mac, sizeof(mac));
    if (err != ESP_OK || mac[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    VigilantWiFiInfo info = {0};
    err = vigilant_get_wifiinfo(&info);
    if (err != ESP_OK) {
        return err;
    }

    for (uint8_t i = 0; i < info.connected_devices_count; ++i) {
        const VigilantWifiDevice* device = &info.connected_devices[i];
        if (strcasecmp(device->mac, mac) != 0) {
            continue;
        }

        if (!device->is_vigilant_device ||
            device->identity != VIGILANT_WIFI_DEVICE_IDENTITY_VIGILANT) {
            return ESP_ERR_INVALID_STATE;
        }

        if (device->address == 0) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (out_device) {
            *out_device = *device;
        }
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

static void send_plain_status(httpd_req_t* req, const char* status,
                              const char* message) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_status(req, status);
    httpd_resp_sendstr(req, message);
}

static esp_err_t wifi_deviceinfo_get_handler(httpd_req_t* req) {
    VigilantWifiDevice device = {0};
    esp_err_t err = find_managed_wifi_device(req, &device);
    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Missing or invalid mac query parameter");
        return ESP_OK;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                            "WiFi client is not a Vigilant device");
        return ESP_OK;
    }
    if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
                            "WiFi client is not connected");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Failed to resolve WiFi client");
        return ESP_OK;
    }

    VeRemoteHttpResponse remote = {0};
    err = remote_http_get(device.address, "/info", &remote);
    if (err != ESP_OK || remote.status_code != 200 || remote.body_len == 0 ||
        remote.body_truncated) {
        ESP_LOGW(TAG, "Failed to fetch /info from %s: err=%s status=%d",
                 device.mac, esp_err_to_name(err), remote.status_code);
        send_plain_status(req, "502 Bad Gateway",
                          "Failed to fetch Vigilant client info");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, remote.body, (ssize_t)remote.body_len);
    return ESP_OK;
}

static esp_err_t wifi_reboot_factory_post_handler(httpd_req_t* req) {
    VigilantWifiDevice device = {0};
    esp_err_t err = find_managed_wifi_device(req, &device);
    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Missing or invalid mac query parameter");
        return ESP_OK;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                            "WiFi client is not a Vigilant device");
        return ESP_OK;
    }
    if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
                            "WiFi client is not connected");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Failed to resolve WiFi client");
        return ESP_OK;
    }

    VeRemoteHttpResponse remote = {0};
    err = remote_http_get(device.address, "/rebootfactory", &remote);
    if (err != ESP_OK || remote.status_code < 200 ||
        remote.status_code >= 300) {
        ESP_LOGW(TAG,
                 "Failed to request recovery reboot from %s: err=%s status=%d",
                 device.mac, esp_err_to_name(err), remote.status_code);
        send_plain_status(req, "502 Bad Gateway",
                          "Failed to request Vigilant client reboot");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Requested recovery reboot from WiFi client %s", device.mac);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static const httpd_uri_t i2cinfo_uri = {
    .uri = "/i2cinfo",
    .method = HTTP_GET,
    .handler = i2cinfo_get_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t wifiinfo_uri = {
    .uri = "/wifiinfo",
    .method = HTTP_GET,
    .handler = wifiinfo_get_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t wifi_deviceinfo_uri = {
    .uri = "/wifi/deviceinfo",
    .method = HTTP_GET,
    .handler = wifi_deviceinfo_get_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t wifi_reboot_factory_uri = {
    .uri = "/wifi/rebootfactory",
    .method = HTTP_POST,
    .handler = wifi_reboot_factory_post_handler,
    .user_ctx = NULL,
};

esp_err_t http_404_error_handler(httpd_req_t* req, httpd_err_code_t err) {
    if (strcmp("/hello", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
                            "/hello URI is not available");
        return ESP_OK;
    } else if (strcmp("/echo", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
                            "/echo URI is not available");
        return ESP_FAIL;
    }
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

static esp_err_t ctrl_put_handler(httpd_req_t* req) {
    char buf;
    int ret;

    if ((ret = httpd_req_recv(req, &buf, 1)) <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    if (buf == '0') {
        ESP_LOGI(TAG, "Unregistering /hello and /echo URIs");
        httpd_unregister_uri(req->handle, "/hello");
        httpd_unregister_uri(req->handle, "/echo");
        httpd_register_err_handler(req->handle, HTTPD_404_NOT_FOUND,
                                   http_404_error_handler);
    } else {
        ESP_LOGI(TAG, "Registering /hello and /echo URIs");
        httpd_register_uri_handler(req->handle, &hello);
        httpd_register_uri_handler(req->handle, &echo);
        httpd_register_err_handler(req->handle, HTTPD_404_NOT_FOUND, NULL);
    }

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t ctrl = {.uri = "/ctrl",
                                 .method = HTTP_PUT,
                                 .handler = ctrl_put_handler,
                                 .user_ctx = NULL};

static void close_socket_with_ws_cleanup(httpd_handle_t hd, int sockfd) {
    websocket_client_closed(sockfd);
    close(sockfd);
}

static httpd_handle_t start_webserver_internal(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers =
        32;  // Fixes issue #28: ota_http: Failed to register Vigilant Dashboard
             // GET handler (ESP_ERR_HTTPD_HANDLERS_FULL)
    config.max_open_sockets = VE_HTTPD_MAX_OPEN_SOCKETS;
    config.backlog_conn = 8;
    config.lru_purge_enable = true;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 30;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 3;
    config.close_fn = close_socket_with_ws_cleanup;

    ESP_LOGI(TAG, "Starting server on port: '%d' with %d open sockets",
             config.server_port, config.max_open_sockets);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &hello);
        httpd_register_uri_handler(server, &echo);
        httpd_register_uri_handler(server, &ctrl);
        httpd_register_uri_handler(server, &any);
        httpd_register_uri_handler(server, &info_uri);
        httpd_register_uri_handler(server, &i2cinfo_uri);
        httpd_register_uri_handler(server, &wifiinfo_uri);
        httpd_register_uri_handler(server, &wifi_deviceinfo_uri);
        httpd_register_uri_handler(server, &wifi_reboot_factory_uri);
        websocket_register_handlers(server);

        // OTA-Handler registrieren
        ota_http_register_handlers(server);

        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

esp_err_t http_server_start(void) {
    if (s_server == NULL) {
        s_server = start_webserver_internal();
        if (s_server == NULL) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t http_server_stop(void) {
    if (s_server) {
        ESP_LOGI(TAG, "Stopping webserver");
        httpd_handle_t tmp = s_server;
        s_server = NULL;
        return httpd_stop(tmp);
    }
    return ESP_OK;
}

#if !CONFIG_IDF_TARGET_LINUX
static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data) {
    (void)arg;
    (void)event_base;
    (void)event_id;
    (void)event_data;

    http_server_start();
}
#endif  // !CONFIG_IDF_TARGET_LINUX

esp_err_t http_server_register_event_handlers(void) {
    // Start server when we get an IP
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &connect_handler, NULL));

    return ESP_OK;
}
