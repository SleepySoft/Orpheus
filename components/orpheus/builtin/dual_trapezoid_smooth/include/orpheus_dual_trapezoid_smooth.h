#ifndef ORPHEUS_DUAL_TRAPEZOID_SMOOTH_H
#define ORPHEUS_DUAL_TRAPEZOID_SMOOTH_H
#include "orpheus_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t channels,bins,batches;
    float fast_alpha,slow_alpha;
    float* history;
    bool owns_memory;
} DualTrapezoidSmoothState;
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);
#ifdef __cplusplus
}
#endif
#endif
