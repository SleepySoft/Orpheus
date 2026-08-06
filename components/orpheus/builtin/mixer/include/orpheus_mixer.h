#ifndef ORPHEUS_MIXER_H
#define ORPHEUS_MIXER_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float gain_linear[2];
    float target_gain_linear[2];
    float smoothing_coeff;
    float gain0_db;        /* 参数缓存（dB，smoothed） */
    float gain1_db;        /* 参数缓存（dB，smoothed） */
    uint32_t channels;
} MixerState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
