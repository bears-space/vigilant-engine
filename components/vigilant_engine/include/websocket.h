// websocket.h
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize log capture so ESP-IDF logs are buffered and can be forwarded
// to websocket clients. Safe to call multiple times.
void websocket_init_log_capture(void);

// Registers websocket URI handlers on the provided HTTP server. Also passes
// the server handle to the websocket module so captured logs can be delivered
// to connected clients.
esp_err_t websocket_register_handlers(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
