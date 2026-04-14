#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t address;
    uint8_t whoami_reg;
    uint8_t expected_whoami;
    i2c_master_dev_handle_t handle;
} VigilantI2CDevice;

#ifdef __cplusplus
}
#endif
