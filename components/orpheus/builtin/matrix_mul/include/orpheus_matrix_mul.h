#ifndef ORPHEUS_MATRIX_MUL_H
#define ORPHEUS_MATRIX_MUL_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORPHEUS_MATRIX_MAX 1024 /* 32×32 */

typedef struct {
    uint32_t rows;
    uint32_t cols;
    float matrix[ORPHEUS_MATRIX_MAX]; /* 行主序：matrix[r*cols+c] */
} MatrixMulState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_MATRIX_MUL_H */
