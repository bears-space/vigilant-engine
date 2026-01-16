#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t ws_mgr_init(httpd_handle_t server);              // einmal nach httpd_start()
esp_err_t ws_mgr_add_client(int sockfd);                   // beim Handshake
esp_err_t ws_mgr_remove_client(int sockfd);                // bei disconnect
esp_err_t ws_mgr_send_text(int sockfd, const char *text);  // send an einen Client
esp_err_t ws_mgr_broadcast_text(const char *text);         // an alle
