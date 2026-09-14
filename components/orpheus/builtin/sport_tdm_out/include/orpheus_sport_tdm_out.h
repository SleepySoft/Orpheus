#ifndef ORPHEUS_SPORT_TDM_OUT_H
#define ORPHEUS_SPORT_TDM_OUT_H

#include "orpheus_abi.h"

typedef struct {
    uint32_t channels;
    uint32_t sample_rate;
    float* dst;
    uint32_t dst_capacity;
} SportTdmOutState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#endif /* ORPHEUS_SPORT_TDM_OUT_H */
