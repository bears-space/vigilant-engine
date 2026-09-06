#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#define FAKE_HTTP_SERVER_MAX_URI_HANDLERS 32

typedef struct {
    esp_err_t start_result;
    esp_err_t stop_result;
    esp_err_t register_result;
    unsigned start_calls;
    unsigned stop_calls;
    unsigned register_calls;
    unsigned unregister_calls;
    unsigned register_error_handler_calls;
    unsigned websocket_register_calls;
    unsigned websocket_client_closed_calls;
    unsigned ota_register_calls;
    httpd_config_t last_config;
    httpd_uri_t registered_handlers[FAKE_HTTP_SERVER_MAX_URI_HANDLERS];
} fake_http_server_t;

extern fake_http_server_t fake_http_server;

void fake_http_server_reset(void);
