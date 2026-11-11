#ifndef ORPHEUS_NOISE_DETECTOR_NLMS_ALGORITHM_H
#define ORPHEUS_NOISE_DETECTOR_NLMS_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float* w;
    float* x;
    uint32_t* pos;
    uint64_t total_calls;
    uint64_t noise_calls;
    uint64_t total_samples;
    uint64_t noisy_samples;
    uint64_t clicks_total;
    double accumulated_input;
    double accumulated_residue;
    char json_detail[1024];
} orpheus_builtin_noise_detector_nlms_AlgorithmState;
#endif
