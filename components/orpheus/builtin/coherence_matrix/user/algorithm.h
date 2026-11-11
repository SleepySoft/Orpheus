#ifndef ORPHEUS_COHERENCE_MATRIX_ALGORITHM_H
#define ORPHEUS_COHERENCE_MATRIX_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float* gxx;
    float* gxy_re;
    float* gxy_im;
    float* re;
    float* im;
    float matrix[100];
    float hist[64];
    uint32_t hist_pos;
    uint32_t hist_count;
    char json[2048];
} orpheus_builtin_coherence_matrix_AlgorithmState;
#endif
