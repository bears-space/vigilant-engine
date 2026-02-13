#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NW_MODE_AP = 0,
    NW_MODE_STA,
    NW_MODE_APSTA
} NW_MODE;

typedef struct {
    char unique_component_name[32];
    NW_MODE network_mode;
} VigilantConfig; 

esp_err_t vigilant_init(VigilantConfig VgConfig);

#ifdef __cplusplus
}
#endif
