// ota_http.h
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// Registriert den /update-Endpoint beim bestehenden HTTP-Server
esp_err_t ota_http_register_handlers(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
