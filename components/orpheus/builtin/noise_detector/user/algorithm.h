#ifndef ORPHEUS_NOISE_DETECTOR_ALGORITHM_H
#define ORPHEUS_NOISE_DETECTOR_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float* win;
    float* re;
    float* im;
    double flatness_ema;
    uint64_t total_samples;
    uint64_t clipped_samples;
    uint64_t clicks_total;
    char json_detail[1024];
} orpheus_builtin_noise_detector_AlgorithmState;
#endif
