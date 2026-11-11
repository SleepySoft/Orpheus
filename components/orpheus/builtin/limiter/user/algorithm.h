#ifndef ORPHEUS_LIMITER_ALGORITHM_H
#define ORPHEUS_LIMITER_ALGORITHM_H
#include <stdint.h>
#define LIMITER_MAX_CHANNELS 32
typedef struct {
    uint32_t mode;
    float threshold_linear,attack_coeff,release_coeff;
    float attack_coeff_per_channel[32],release_coeff_per_channel[32],k1[32],max_attack[32],env[32];
    float feedback_attack[64],feedback_release[64],feedback_k1[64],feedback_min_factor[64];
} orpheus_builtin_limiter_AlgorithmState;
#endif
