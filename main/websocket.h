// websocket.h
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// Registers websocket URI handlers on the provided HTTP server.
esp_err_t websocket_register_handlers(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
