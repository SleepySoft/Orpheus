#ifndef ORPHEUS_EXPONENTIAL_GAIN_H
#define ORPHEUS_EXPONENTIAL_GAIN_H
#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t channels;
    float target_gain, ramp_coefficient, remaining_blocks;
    float initial_gain, min_gain, snap_threshold;
    float current_gain, delayed_target, filter_state;
    bool load_initial, owns_state;
} ExponentialGainState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);
#ifdef __cplusplus
}
#endif
#endif
