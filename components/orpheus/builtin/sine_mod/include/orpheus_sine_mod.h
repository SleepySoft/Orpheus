#ifndef ORPHEUS_SINE_MOD_H
#define ORPHEUS_SINE_MOD_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SINE_MOD_MAX_TABLE 1024

typedef struct {
    float freq_hz;
    float depth;
    float phase;       /* 0..2π 振荡器相位 */
    float phase_inc;
    float sample_rate;
    uint32_t channels;
    int32_t modulation_mode;
    int32_t table_length;
    uint32_t table_index;
    float carrier_table[SINE_MOD_MAX_TABLE];
} SineModState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_SINE_MOD_H */
