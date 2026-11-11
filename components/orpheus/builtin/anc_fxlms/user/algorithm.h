#ifndef ORPHEUS_ANC_FXLMS_ALGORITHM_H
#define ORPHEUS_ANC_FXLMS_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float* w;
    float* xbuf;
    float* xf;
    uint32_t* pos;
    double acc_d2;
    double acc_e2;
    uint64_t total_frames;
    uint64_t total_samples;
    char json_detail[512];
} orpheus_builtin_anc_fxlms_AlgorithmState;
#endif
