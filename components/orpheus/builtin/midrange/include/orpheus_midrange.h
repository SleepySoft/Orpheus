#ifndef ORPHEUS_MIDRANGE_H
#define ORPHEUS_MIDRANGE_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CHANNELS 32

typedef struct {
    float b0, a1, a2;            /* 2nd-order bandpass coeffs (b2 = -b0) */
    float z1[MAX_CHANNELS];      /* per-channel DF2T state 1 */
    float z2[MAX_CHANNELS];      /* per-channel DF2T state 2 */
    float boost;
    float target_boost;
    float smoothing_coeff;
    float gain_db;               /* 参数缓存（smoothed） */
    float fc;                    /* 参数缓存（restart_required） */
    float q;                     /* 参数缓存 */
    float ramp_ms;               /* 参数缓存 */
    uint32_t channels;
} MidRangeState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_MIDRANGE_H */
