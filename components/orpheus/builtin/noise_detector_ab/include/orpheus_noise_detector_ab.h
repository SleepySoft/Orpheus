#ifndef ORPHEUS_NOISE_DETECTOR_AB_H
#define ORPHEUS_NOISE_DETECTOR_AB_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NDAB_MAX_CH 32
#define NDAB_MAX_FFT 4096
#define NDAB_MAX_BANDS 3

typedef struct {
    uint32_t channels;
    uint32_t fft_size;      /* internal FFT window (pow2, <= block) */
    uint32_t half;          /* persistent bins = fft_size/2 */
    float alpha;            /* PSD EMA smoothing coefficient */
    float noise_thresh_db;  /* frame-level distortion+noise threshold */
    float time_thres;       /* time-domain residual amplitude threshold */
    float* sxx;             /* per-channel x half */
    float* syy;
    float* sxy_re;          /* per-channel x half */
    float* sxy_im;
    float* win;             /* hann window, fft_size */
    float* rea;
    float* ima;             /* fft temp per channel */
    /* band edges (bins) for low/mid/high */
    uint32_t lo_end, mid_end; /* lo: [0,lo_end) mid:[lo_end,mid_end) hi:[mid_end,half) */
    /* counters */
    uint64_t total_frames;
    uint32_t noise_frames;
    float noise_ratio;   /* computed noise_frames/total_frames, vendor for probe slot */
    uint64_t total_samples;
    uint64_t noisy_samples;
    uint32_t clicks;
    float thd_n_db;
    float coh_lo, coh_mid, coh_hi;
    float residue_pk;
    char json_detail[1024];
} NoiseDetectorAbState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
