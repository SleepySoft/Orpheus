#ifndef ORPHEUS_RESAMPLE_H
#define ORPHEUS_RESAMPLE_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RESAMPLE_MAX_CHANNELS 32

typedef struct {
    uint32_t factor;
    uint32_t channels;
    float acc[RESAMPLE_MAX_CHANNELS];
    uint32_t n;          /* input samples accumulated in current group */
    uint32_t write_pos;  /* frames written into the output block */
} ResampleState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
