#ifndef ORPHEUS_NOISE_SLEW_H
#define ORPHEUS_NOISE_SLEW_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float rise_rate;
    float fall_rate;
    float rise_delta;   /* 每样本最大增量 */
    float fall_delta;   /* 每样本最大减量 */
    float prev[32];
    uint32_t channels;
} NoiseSlewState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_NOISE_SLEW_H */
