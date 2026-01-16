// http_server.h
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// Startet den HTTP-Server (idempotent)
esp_err_t http_server_start(void);

// Stoppt den HTTP-Server (idempotent)
esp_err_t http_server_stop(void);

// Gibt den aktuellen Server-Handle zurück (oder NULL wenn nicht gestartet)
httpd_handle_t http_server_get_handle(void);

// Registriert eine URI auf dem laufenden Server
esp_err_t http_server_register_uri(const httpd_uri_t *uri);

// Registriert WiFi/Ethernet Event-Handler (auto start/stop)
esp_err_t http_server_register_event_handlers(void);

#ifdef __cplusplus
}
#endif
