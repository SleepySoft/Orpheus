#ifndef ORPHEUS_FIR_H
#define ORPHEUS_FIR_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FIR_MAX_TAPS 1024

typedef struct {
    uint32_t channels;
    uint32_t taps;      /* 探针（PROBE） */
    float* coeffs;      /* taps 个系数（prepare 分配） */
    float* delay;       /* taps * channels，环形延迟线 */
    uint32_t* pos;      /* 每通道写入位置 */
} FirState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
