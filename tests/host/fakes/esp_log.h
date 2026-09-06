#pragma once

#include <stdio.h>

/* Keep arguments type-checked and considered used, without printing logs. */
#define ESP_LOGI(tag, ...)          \
    do {                            \
        (void)(tag);                \
        if (0) printf(__VA_ARGS__); \
    } while (0)
#define ESP_LOGW(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
