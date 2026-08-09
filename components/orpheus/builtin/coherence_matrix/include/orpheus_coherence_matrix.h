#ifndef ORPHEUS_COHERENCE_MATRIX_H
#define ORPHEUS_COHERENCE_MATRIX_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CM_MAX_CHANNELS 10
#define CM_MAX_HALF 2048
#define CM_HISTORY 64

typedef struct {
    uint32_t channels;
    uint32_t half;
    float alpha;
    float* gxx;         /* channels × half */
    float* gxy_re;      /* channels × channels × half */
    float* gxy_im;
    float* rea;         /* channels × half（每通道 FFT 实部） */
    float* ima;
    float hist[CM_HISTORY];
    uint32_t hist_pos;
    uint32_t hist_count;
    char json_matrix[8192];
    char json_history[2048];
} CoherenceMatrixState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_COHERENCE_MATRIX_H */
