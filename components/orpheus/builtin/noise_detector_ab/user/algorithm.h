#ifndef ORPHEUS_NOISE_DETECTOR_AB_ALGORITHM_H
#define ORPHEUS_NOISE_DETECTOR_AB_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float* sxx;
    float* syy;
    float* sxy_re;
    float* sxy_im;
    float* win;
    float* re;
    float* im;
    uint64_t total_windows;
    uint64_t noise_windows;
    uint64_t total_samples;
    uint64_t noisy_samples;
    uint64_t clicks_total;
    float coherence[3];
    char json_detail[1024];
} orpheus_builtin_noise_detector_ab_AlgorithmState;
#endif
