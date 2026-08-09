#ifndef ORPHEUS_SWITCH_H
#define ORPHEUS_SWITCH_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float enable;        /* 调音参数：0=静音 1=直通 */
    float gain;          /* 当前增益（斜坡过渡中） */
    float target;
    float smoothing_coeff;
    float ramp_ms;
    uint32_t channels;
} SwitchState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_SWITCH_H */
