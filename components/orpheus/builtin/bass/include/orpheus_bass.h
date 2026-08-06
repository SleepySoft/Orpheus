#ifndef ORPHEUS_BASS_H
#define ORPHEUS_BASS_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CHANNELS 32

typedef struct {
    float a1;               /* 1st-order LPF pole */
    float z[MAX_CHANNELS];  /* per-channel LPF state */
    float boost;            /* current boost amount (ramping) */
    float target_boost;     /* target boost amount */
    float smoothing_coeff;
    float gain_db;          /* 参数缓存（smoothed） */
    float fc;               /* 参数缓存（restart_required） */
    float ramp_ms;          /* 参数缓存 */
    uint32_t channels;
} BassState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_BASS_H */
