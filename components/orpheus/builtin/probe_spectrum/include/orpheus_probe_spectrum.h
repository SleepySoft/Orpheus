#ifndef ORPHEUS_PROBE_SPECTRUM_H
#define ORPHEUS_PROBE_SPECTRUM_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPECTRUM_MAX_WINDOW 4096
#define SPECTRUM_PROBE_BINS 1023

typedef struct {
    uint32_t channels;
    uint32_t window_size;
    uint32_t half;
    float* window;  /* Hann 窗 */
    float* ring;    /* 第 0 通道环形缓冲 */
    uint32_t ring_pos;
    uint32_t samples_seen;
    float* re;      /* FFT 实部 */
    float* im;      /* FFT 虚部 */
    float* mags;    /* half 个幅度 */
    float probe[SPECTRUM_PROBE_BINS]; /* 重采样到完整 Nyquist 频段 */
} SpectrumState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
