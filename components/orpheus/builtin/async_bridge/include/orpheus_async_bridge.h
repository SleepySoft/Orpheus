#ifndef ORPHEUS_ASYNC_BRIDGE_H
#define ORPHEUS_ASYNC_BRIDGE_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t channels;
    uint32_t capacity_frames;
    int32_t level_frames;
    int32_t underruns;
    int32_t overruns;
} AsyncBridgeState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_ASYNC_BRIDGE_H */
