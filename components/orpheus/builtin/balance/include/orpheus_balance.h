#ifndef ORPHEUS_BALANCE_H
#define ORPHEUS_BALANCE_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float left_gain;
    float right_gain;
    float target_left;
    float target_right;
    float balance;       /* 参数（smoothed） */
    float smoothing_coeff;
    float ramp_ms;       /* 参数缓存（restart_required） */
    uint32_t channels;
} BalanceState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_BALANCE_H */
