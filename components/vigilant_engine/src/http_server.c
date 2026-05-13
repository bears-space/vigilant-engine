// http_server.c

#include "http_server.h"

#include <esp_system.h>
#include <esp_wifi.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
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

static void uri_decode(char* dest, const char* src, size_t len) {
    if (!dest || !src) {
        return;
    }

    size_t rd = 0;
    size_t wr = 0;
    while (rd < len && src[rd] != '\0') {
        if (src[rd] == '%' && (rd + 2) < len) {
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
                uri_decode(dec_param, param,
                           strnlen(param, HTTP_QUERY_KEY_MAX_LEN));
                ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
            }
            if (httpd_query_key_value(buf, "query3", param, sizeof(param)) ==
                ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query3=%s", param);
                uri_decode(dec_param, param,
                           strnlen(param, HTTP_QUERY_KEY_MAX_LEN));
                ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
            }
            if (httpd_query_key_value(buf, "query2", param, sizeof(param)) ==
                ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query2=%s", param);
                uri_decode(dec_param, param,
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
    char payload[256];
    int written = snprintf(
        payload, sizeof(payload),
        "{\"name\":\"%s\",\"network_mode\":%d,\"mac\":\"%s\",\"ap_ssid\":\"%"
        "s\",\"sta_ssid\":\"%s\",\"ip_sta\":\"%s\",\"ip_ap\":\"%s\"}",
        info.unique_component_name, (int)info.network_mode, info.mac,
        info.ap_ssid, info.sta_ssid, info.ip_sta, info.ip_ap);

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
    char payload[384];
    int written = snprintf(
        payload, sizeof(payload),
        "{\"network_mode\":%d,\"mac\":\"%s\",\"ap_ssid\":\"%s\",\"sta_ssid\":"
        "\"%s\",\"ip_sta\":\"%s\",\"ip_ap\":\"%s\",\"connected_devices_count\":"
        "%u}",
        (int)info.network_mode, info.mac, info.ap_ssid, info.sta_ssid,
        info.ip_sta, info.ip_ap, (unsigned int)info.connected_devices_count);

    if (written < 0 || written >= (int)sizeof(payload)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Info too large");
        return ESP_FAIL;
    }

    httpd_resp_send(req, payload, written);
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
