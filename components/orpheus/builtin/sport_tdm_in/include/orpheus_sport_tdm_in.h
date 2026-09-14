#ifndef ORPHEUS_SPORT_TDM_IN_H
#define ORPHEUS_SPORT_TDM_IN_H

#include "orpheus_abi.h"

typedef struct {
    uint32_t channels;
    uint32_t sample_rate;
    const float* src;
    uint32_t src_frames;
    uint32_t underruns;
} SportTdmInState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#endif /* ORPHEUS_SPORT_TDM_IN_H */
