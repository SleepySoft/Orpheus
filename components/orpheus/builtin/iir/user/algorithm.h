#ifndef ORPHEUS_IIR_ALGORITHM_H
#define ORPHEUS_IIR_ALGORITHM_H
typedef struct {
    float shared[33];
    float per_channel[2112];
    float history[64][16];
} orpheus_builtin_iir_AlgorithmState;
#endif
