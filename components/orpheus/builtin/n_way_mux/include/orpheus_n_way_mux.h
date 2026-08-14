#ifndef ORPHEUS_N_WAY_MUX_H
#define ORPHEUS_N_WAY_MUX_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t channels;
    uint32_t inputs;       /* input pin count (restart_required, affects_signature) */
    float select;          /* target selected input index (smoothed, 0..N-1) */
    float select_smoothed; /* current smoothed index */
    float ramp_coeff;      /* one-pole smoothing coeff (from ramp_ms) */
} NWayMuxState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
