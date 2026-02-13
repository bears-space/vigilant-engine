// http_server.h
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Startet den HTTP-Server (idempotent, wird nur einmal gestartet)
esp_err_t http_server_start(void);

// Stoppt den HTTP-Server (idempotent)
esp_err_t http_server_stop(void);

// Registriert die Event-Handler für WiFi/Ethernet,
// damit der Server bei Verbindungsänderungen neu gestartet/gestoppt wird.
esp_err_t http_server_register_event_handlers(void);

#ifdef __cplusplus
}
#endif
