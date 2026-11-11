#ifndef ORPHEUS_ADAPTIVE_FIR_ALGORITHM_H
#define ORPHEUS_ADAPTIVE_FIR_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float* w;
    float* xbuf;
    float* dbuf;
    uint32_t* pos;
    uint64_t total_frames;
    char json_detail[512];
} orpheus_builtin_adaptive_fir_AlgorithmState;
#endif
