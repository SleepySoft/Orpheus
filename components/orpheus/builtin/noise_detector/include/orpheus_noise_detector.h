#ifndef ORPHEUS_NOISE_DETECTOR_H
#define ORPHEUS_NOISE_DETECTOR_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ND_MAX_CH 32
#define ND_MAX_FFT 4096

typedef struct {
    uint32_t channels;
    uint32_t fft_size;
    uint32_t half;
    float* win;
    float* rea;
    float* ima;
    /* real scalar probes */
    float flatness;
    float noise_floor_db;
    uint32_t clicks;
    float clip_ratio;       /* clipped_samples/total_samples */
    float clip_level;
    float click_thres;
    /* internal accumulators */
    double flatness_ema;
    uint64_t total_samples;
    uint64_t clipped_samples;
    char json_detail[1024];
} NoiseDetectorState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
