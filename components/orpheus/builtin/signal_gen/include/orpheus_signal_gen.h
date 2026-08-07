#ifndef ORPHEUS_SIGNAL_GEN_H
#define ORPHEUS_SIGNAL_GEN_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float phase;
    float frequency;   /* 参数（restart_required） */
    float amplitude;   /* 参数（smoothed，process 每块读取） */
    uint32_t channels;
    uint32_t sample_rate;  /* 参数：时钟源声明，成为图采样率 */
} SignalGenState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
