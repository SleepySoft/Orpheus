#ifndef ORPHEUS_IIR_BANK_ALGORITHM_H
#define ORPHEUS_IIR_BANK_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float z1[16];
    float z2[16];
} orpheus_builtin_iir_bank_ChannelState;
typedef struct {
    float shared[80];
    float per_channel[5120];
    orpheus_builtin_iir_bank_ChannelState channelStates[64];
    uint32_t channelStages[64];
} orpheus_builtin_iir_bank_AlgorithmState;
#endif
