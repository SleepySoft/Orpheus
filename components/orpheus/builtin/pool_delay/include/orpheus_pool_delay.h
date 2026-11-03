#ifndef ORPHEUS_POOL_DELAY_H
#define ORPHEUS_POOL_DELAY_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POOL_DELAY_MAX_CHANNELS 64
#define POOL_DELAY_MAX_SAMPLES 192000

typedef struct {
    float* buffer;
    uint32_t channels;
    uint32_t pool_size;
    uint32_t cursor;
    int32_t delays_i32[POOL_DELAY_MAX_CHANNELS];
} PoolDelayState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
