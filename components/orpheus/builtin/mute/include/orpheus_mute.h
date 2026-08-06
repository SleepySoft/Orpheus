#ifndef ORPHEUS_MUTE_H
#define ORPHEUS_MUTE_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float mute;            /* 参数：0=active 1=muted */
    float gain_linear;     /* 当前增益（ramping） */
    float target_linear;
    float smoothing_coeff;
    float ramp_ms;         /* 参数缓存（restart_required） */
    uint32_t channels;
} MuteState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_MUTE_H */
