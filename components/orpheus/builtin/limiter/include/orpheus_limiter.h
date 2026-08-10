#ifndef ORPHEUS_LIMITER_H
#define ORPHEUS_LIMITER_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIMITER_MAX_CHANNELS 32

typedef struct {
    uint32_t channels;
    uint32_t mode;                 /* 0 = shared, 1 = per_channel */
    float threshold_db;
    float threshold_linear;

    /* shared 模式 */
    float attack_ms;
    float release_ms;
    float attack_coeff;
    float release_coeff;

    /* per_channel 模式 */
    float attack_coeff_per_channel[LIMITER_MAX_CHANNELS];
    float release_coeff_per_channel[LIMITER_MAX_CHANNELS];
    float k1[LIMITER_MAX_CHANNELS];
    float max_attack[LIMITER_MAX_CHANNELS];
    float env[LIMITER_MAX_CHANNELS];

    float gain;                    /* 当前增益（兼容 get_state_value） */
} LimiterState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_LIMITER_H */
