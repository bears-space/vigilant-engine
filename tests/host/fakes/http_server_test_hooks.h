#pragma once

#include <stddef.h>

int http_server_test_hex_nibble(char c);
void http_server_test_uri_decode(char* dest, const char* src, size_t len);
