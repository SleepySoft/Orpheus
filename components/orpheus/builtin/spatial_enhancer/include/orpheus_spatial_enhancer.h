#ifndef ORPHEUS_SPATIAL_ENHANCER_H
#define ORPHEUS_SPATIAL_ENHANCER_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t channels;
    float width;            /* side gain target (1.0 = neutral, 0..5) */
    float width_smoothed;
    float air;              /* side high-pass air gain target (0..1) */
    float air_smoothed;
    float mono_mix;         /* how much of side to recombine (0=full mid/mono, 1=full side) */
    float mono_mix_smoothed;
    float air_fc;           /* air high-pass cutoff (Hz), restart param cache */
    float mono_coeff;       /* first-order smoothing coefficient for all knobs */
    /* first-order high-pass state (per channel pair) for "air" on side */
    float hp_prev_side;     /* previous side sample (feedforward) */
    float hp_prev_out;      /* previous high-passed side sample (feedback) */
    float hp_fb;            /* one-pole feedback coefficient (alpha) */
    float hp_gain;          /* one-pole feedforward gain b0=(1+alpha)/2 */
} SpatialEnhancerState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
