#ifndef ORPHEUS_OVERLAP_ADD_H
#define ORPHEUS_OVERLAP_ADD_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t channels;
    uint32_t frame_size;
    uint32_t hop_size;
    uint32_t num_frames;
    float* history;
    bool owns_state;
} OverlapAddState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif
#endif
