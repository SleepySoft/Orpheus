#ifndef ORPHEUS_FRAME_ENERGY_H
#define ORPHEUS_FRAME_ENERGY_H
#include "orpheus_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t channels,frames;
    bool owns_memory;
} FrameEnergyState;
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);
#ifdef __cplusplus
}
#endif
#endif
