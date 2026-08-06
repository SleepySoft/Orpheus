#ifndef ORPHEUS_BIQUAD_H
#define ORPHEUS_BIQUAD_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CHANNELS 32

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1[MAX_CHANNELS];
    float z2[MAX_CHANNELS];
    char type[16];       /* 参数缓存（restart_required） */
    float fc;            /* 参数缓存 */
    float q;             /* 参数缓存 */
    float gain_db;       /* 参数缓存 */
    uint32_t channels;
} BiquadState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
