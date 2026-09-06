#pragma once

/*
 * Host-only, source-compatible subset of ESP-IDF's esp_http_server.h.
 * Keep this limited to declarations used by Vigilant Engine and its tests.
 * It is a test double, not an implementation of the ESP-IDF API or ABI.
 */

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "esp_err.h"

#ifndef CONFIG_HTTPD_MAX_URI_LEN
#define CONFIG_HTTPD_MAX_URI_LEN 512
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* httpd_handle_t;
typedef void (*httpd_free_ctx_fn_t)(void* context);
typedef void (*httpd_close_func_t)(httpd_handle_t handle, int socket_fd);

typedef enum {
    HTTP_DELETE = 0,
    HTTP_GET,
    HTTP_HEAD,
    HTTP_POST,
    HTTP_PUT,
    HTTP_CONNECT,
    HTTP_OPTIONS,
    HTTP_TRACE,
    HTTP_PATCH,
} httpd_method_t;

#define HTTP_ANY INT_MAX

typedef enum {
    HTTPD_500_INTERNAL_SERVER_ERROR = 0,
    HTTPD_501_METHOD_NOT_IMPLEMENTED,
    HTTPD_505_VERSION_NOT_SUPPORTED,
    HTTPD_400_BAD_REQUEST,
    HTTPD_401_UNAUTHORIZED,
    HTTPD_403_FORBIDDEN,
    HTTPD_404_NOT_FOUND,
    HTTPD_405_METHOD_NOT_ALLOWED,
    HTTPD_408_REQ_TIMEOUT,
    HTTPD_411_LENGTH_REQUIRED,
    HTTPD_413_CONTENT_TOO_LARGE,
    HTTPD_414_URI_TOO_LONG,
    HTTPD_431_REQ_HDR_FIELDS_TOO_LARGE,
    HTTPD_ERR_CODE_MAX,
} httpd_err_code_t;

typedef struct httpd_req {
    httpd_handle_t handle;
    int method;
    const char uri[CONFIG_HTTPD_MAX_URI_LEN + 1];
    size_t content_len;
    void* aux;
    void* user_ctx;
    void* sess_ctx;
    httpd_free_ctx_fn_t free_ctx;
    bool ignore_sess_ctx_changes;
} httpd_req_t;

typedef esp_err_t (*httpd_uri_func_t)(httpd_req_t* request);
typedef esp_err_t (*httpd_err_handler_func_t)(httpd_req_t* request,
                                              httpd_err_code_t error);

typedef struct {
    const char* uri;
    httpd_method_t method;
    httpd_uri_func_t handler;
    void* user_ctx;
} httpd_uri_t;

typedef struct {
    uint16_t server_port;
    uint16_t max_open_sockets;
    uint16_t max_uri_handlers;
    uint16_t backlog_conn;
    bool lru_purge_enable;
    bool keep_alive_enable;
    int keep_alive_idle;
    int keep_alive_interval;
    int keep_alive_count;
    httpd_close_func_t close_fn;
} httpd_config_t;

#define HTTPD_DEFAULT_CONFIG()      \
    {                               \
        .server_port = 80,          \
        .max_open_sockets = 7,      \
        .max_uri_handlers = 8,      \
        .backlog_conn = 5,          \
        .lru_purge_enable = false,  \
        .keep_alive_enable = false, \
        .keep_alive_idle = 0,       \
        .keep_alive_interval = 0,   \
        .keep_alive_count = 0,      \
        .close_fn = NULL,           \
    }

#define HTTPD_RESP_USE_STRLEN ((ssize_t) - 1)
#define HTTPD_SOCK_ERR_TIMEOUT (-3)

esp_err_t httpd_start(httpd_handle_t* handle, const httpd_config_t* config);
esp_err_t httpd_stop(httpd_handle_t handle);

esp_err_t httpd_register_uri_handler(httpd_handle_t handle,
                                     const httpd_uri_t* uri_handler);
esp_err_t httpd_unregister_uri(httpd_handle_t handle, const char* uri);
esp_err_t httpd_register_err_handler(httpd_handle_t handle,
                                     httpd_err_code_t error,
                                     httpd_err_handler_func_t handler);

size_t httpd_req_get_hdr_value_len(httpd_req_t* request, const char* field);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t* request, const char* field,
                                      char* value, size_t value_size);
size_t httpd_req_get_url_query_len(httpd_req_t* request);
esp_err_t httpd_req_get_url_query_str(httpd_req_t* request, char* buffer,
                                      size_t buffer_size);
esp_err_t httpd_query_key_value(const char* query, const char* key, char* value,
                                size_t value_size);
int httpd_req_recv(httpd_req_t* request, char* buffer, size_t buffer_size);

esp_err_t httpd_resp_set_hdr(httpd_req_t* request, const char* field,
                             const char* value);
esp_err_t httpd_resp_set_type(httpd_req_t* request, const char* type);
esp_err_t httpd_resp_send(httpd_req_t* request, const char* buffer,
                          ssize_t buffer_length);
esp_err_t httpd_resp_send_chunk(httpd_req_t* request, const char* buffer,
                                ssize_t buffer_length);
esp_err_t httpd_resp_send_err(httpd_req_t* request, httpd_err_code_t error,
                              const char* message);
esp_err_t httpd_resp_send_408(httpd_req_t* request);

#ifdef __cplusplus
}
#endif
