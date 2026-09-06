#pragma once

#include <stddef.h>
#include "esp_err.h"

int http_server_test_hex_nibble(char c);
esp_err_t http_server_test_uri_decode(char* dest, const char* src, size_t len);
