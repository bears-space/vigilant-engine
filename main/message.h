#pragma once
#ifdef __cplusplus
extern "C" {
#endif
 
#include <stdint.h>

struct message_t {
    uint8_t *data; // max packet size for LLCC68 is 255 bytes, and 254 with address filtering, but we don't use address filtering
    size_t length;
};

#ifdef __cplusplus
}
#endif