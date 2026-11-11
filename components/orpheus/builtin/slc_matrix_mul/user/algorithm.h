#ifndef ORPHEUS_SLC_MATRIX_ALGORITHM_H
#define ORPHEUS_SLC_MATRIX_ALGORITHM_H
#include <stdbool.h>
#define SLC_MM_SILENT_GAIN 5.0118723e-7f
typedef struct {
    float tables[8192];
    float interp_x[8];
    float requested_gains[1024];
    float last_index;
    float active_coefficient;
} orpheus_builtin_slc_matrix_mul_AlgorithmState;
#endif
