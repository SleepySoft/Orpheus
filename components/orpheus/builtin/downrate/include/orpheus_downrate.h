#ifndef ORPHEUS_DOWNRATE_H
#define ORPHEUS_DOWNRATE_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t factor;
    uint32_t channels;
    uint32_t offset_frames;
} DownrateState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
