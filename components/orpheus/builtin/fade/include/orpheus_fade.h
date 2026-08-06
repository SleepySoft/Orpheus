#ifndef ORPHEUS_FADE_H
#define ORPHEUS_FADE_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CHANNELS 32

typedef struct {
    float a1;                 /* 1st-order LPF pole (crossover) */
    float z[MAX_CHANNELS];    /* per-channel LPF state */
    float front_gain;         /* current front gain (ramping) */
    float back_gain;          /* current back gain (ramping) */
    float target_front;
    float target_back;
    float fade;               /* 参数（smoothed） */
    float smoothing_coeff;
    float crossover_hz;       /* 参数缓存（restart_required） */
    float ramp_ms;            /* 参数缓存（restart_required） */
    uint32_t channels;
    uint32_t front_channels;  /* 0..front_channels-1 are front */
} FadeState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_FADE_H */
