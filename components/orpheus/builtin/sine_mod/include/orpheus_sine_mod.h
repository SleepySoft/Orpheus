#ifndef ORPHEUS_SINE_MOD_H
#define ORPHEUS_SINE_MOD_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float freq_hz;
    float depth;
    float phase;       /* 0..2π 振荡器相位 */
    float phase_inc;
    float sample_rate;
    uint32_t channels;
} SineModState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_SINE_MOD_H */
