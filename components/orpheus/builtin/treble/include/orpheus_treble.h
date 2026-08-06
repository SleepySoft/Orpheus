#ifndef ORPHEUS_TREBLE_H
#define ORPHEUS_TREBLE_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CHANNELS 32

typedef struct {
    float a1;
    float z[MAX_CHANNELS];
    float boost;
    float target_boost;
    float smoothing_coeff;
    float gain_db;          /* 参数缓存（smoothed） */
    float fc;               /* 参数缓存（restart_required） */
    float ramp_ms;          /* 参数缓存 */
    uint32_t channels;
} TrebleState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_TREBLE_H */
