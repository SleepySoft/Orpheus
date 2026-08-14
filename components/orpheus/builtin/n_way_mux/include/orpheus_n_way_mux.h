#ifndef ORPHEUS_N_WAY_MUX_H
#define ORPHEUS_N_WAY_MUX_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t channels;
    uint32_t inputs;         /* input pin count (restart_required, affects_signature) */
    uint32_t select;         /* target input, 1-based: 1 = in0 ... N = in(N-1) */
    float select_pos;        /* current 0-based position, linearly ramped toward select-1 */
    float ramp_step;         /* per-sample position step derived from ramp_ms */
} NWayMuxState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
