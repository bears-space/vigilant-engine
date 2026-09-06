#include "fake_http_server.h"

#include <string.h>

#include "ota_http.h"
#include "vigilant.h"
#include "websocket.h"

static int server_storage;
fake_http_server_t fake_http_server;

void fake_http_server_reset(void) {
    memset(&fake_http_server, 0, sizeof(fake_http_server));
}

esp_err_t httpd_start(httpd_handle_t* handle, const httpd_config_t* config) {
    fake_http_server.start_calls++;
    fake_http_server.last_config = *config;

    if (fake_http_server.start_result == ESP_OK) {
        *handle = &server_storage;
    }

    return fake_http_server.start_result;
}

esp_err_t httpd_stop(httpd_handle_t handle) {
    (void)handle;
    fake_http_server.stop_calls++;
    return fake_http_server.stop_result;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t handle,
                                     const httpd_uri_t* uri_handler) {
    (void)handle;

    unsigned index = fake_http_server.register_calls++;
    if (uri_handler && index < FAKE_HTTP_SERVER_MAX_URI_HANDLERS) {
        fake_http_server.registered_handlers[index] = *uri_handler;
    }

    return fake_http_server.register_result;
}

esp_err_t httpd_unregister_uri(httpd_handle_t handle, const char* uri) {
    (void)handle;
    (void)uri;
    fake_http_server.unregister_calls++;
    return ESP_OK;
}

esp_err_t httpd_register_err_handler(httpd_handle_t handle,
                                     httpd_err_code_t error,
                                     httpd_err_handler_func_t handler) {
    (void)handle;
    (void)error;
    (void)handler;
    fake_http_server.register_error_handler_calls++;
    return ESP_OK;
}

size_t httpd_req_get_hdr_value_len(httpd_req_t* request, const char* field) {
    (void)request;
    (void)field;
    return 0;
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t* request, const char* field,
                                      char* value, size_t value_size) {
    (void)request;
    (void)field;
    if (value && value_size > 0) {
        value[0] = '\0';
    }
    return ESP_ERR_NOT_FOUND;
}

size_t httpd_req_get_url_query_len(httpd_req_t* request) {
    (void)request;
    return 0;
}

esp_err_t httpd_req_get_url_query_str(httpd_req_t* request, char* buffer,
                                      size_t buffer_size) {
    (void)request;
    if (buffer && buffer_size > 0) {
        buffer[0] = '\0';
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t httpd_query_key_value(const char* query, const char* key, char* value,
                                size_t value_size) {
    (void)query;
    (void)key;
    if (value && value_size > 0) {
        value[0] = '\0';
    }
    return ESP_ERR_NOT_FOUND;
}

int httpd_req_recv(httpd_req_t* request, char* buffer, size_t buffer_size) {
    (void)request;
    (void)buffer;
    (void)buffer_size;
    return ESP_FAIL;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t* request, const char* field,
                             const char* value) {
    (void)request;
    (void)field;
    (void)value;
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t* request, const char* type) {
    (void)request;
    (void)type;
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t* request, const char* buffer,
                          ssize_t buffer_length) {
    (void)request;
    (void)buffer;
    (void)buffer_length;
    return ESP_OK;
}

esp_err_t httpd_resp_send_chunk(httpd_req_t* request, const char* buffer,
                                ssize_t buffer_length) {
    (void)request;
    (void)buffer;
    (void)buffer_length;
    return ESP_OK;
}

esp_err_t httpd_resp_send_err(httpd_req_t* request, httpd_err_code_t error,
                              const char* message) {
    (void)request;
    (void)error;
    (void)message;
    return ESP_OK;
}

esp_err_t httpd_resp_send_408(httpd_req_t* request) {
    (void)request;
    return ESP_OK;
}

esp_err_t websocket_register_handlers(httpd_handle_t server) {
    (void)server;
    fake_http_server.websocket_register_calls++;
    return ESP_OK;
}

void websocket_client_closed(int fd) {
    (void)fd;
    fake_http_server.websocket_client_closed_calls++;
}

esp_err_t ota_http_register_handlers(httpd_handle_t server) {
    (void)server;
    fake_http_server.ota_register_calls++;
    return ESP_OK;
}

esp_err_t vigilant_get_info(VigilantInfo* info) {
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(info, 0, sizeof(*info));
    return ESP_OK;
}

esp_err_t vigilant_get_i2cinfo(VigilantI2cInfo* info) {
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(info, 0, sizeof(*info));
    return ESP_OK;
}
