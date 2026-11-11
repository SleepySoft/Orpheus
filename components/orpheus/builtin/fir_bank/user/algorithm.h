#ifndef ORPHEUS_FIR_BANK_ALGORITHM_H
#define ORPHEUS_FIR_BANK_ALGORITHM_H
#include <stdint.h>
typedef struct {
    /* 条件资源互斥，复用同一份系数存储。 */
    union {
        float shared[4096];
        float per_channel[32768];
        float banked[32768];
    } coefficients;
    float* history;
    uint32_t lengths[64];
    uint32_t positions[64];
} orpheus_builtin_fir_bank_AlgorithmState;
#endif
