#ifndef ORPHEUS_LIMITER_H
#define ORPHEUS_LIMITER_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float threshold_db;
    float threshold_linear;
    float attack_ms;
    float release_ms;
    float attack_coeff;
    float release_coeff;
    float env;           /* 当前峰值包络 */
    float gain;          /* 当前增益（用于 get_state_value） */
    uint32_t channels;
} LimiterState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_LIMITER_H */
