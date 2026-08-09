#ifndef ORPHEUS_IIR_BANK_H
#define ORPHEUS_IIR_BANK_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IIR_BANK_MAX_STAGES    16
#define IIR_BANK_MAX_CHANNELS  32

/* 每级仅保存状态（z1/z2），系数从连续 coefs[] 数组读取 */
typedef struct {
    float z1[IIR_BANK_MAX_CHANNELS];
    float z2[IIR_BANK_MAX_CHANNELS];
} IirStageState;

typedef struct {
    /* BULK 直写目标：连续系数 [b0,b1,b2,a1,a2] x numStages，共 5x16=80 float */
    float coefs[5 * IIR_BANK_MAX_STAGES];
    IirStageState stages[IIR_BANK_MAX_STAGES];
    uint32_t numStages;
    uint32_t channels;
} IirBankState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_IIR_BANK_H */
