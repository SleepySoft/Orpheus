#ifndef ORPHEUS_TRAPEZOID_RATIO_H
#define ORPHEUS_TRAPEZOID_RATIO_H
#include "orpheus_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t channels,bins,batches,division_mode;
    float alpha,epsilon;
    float* history;
    bool owns_memory;
} TrapezoidRatioState;
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);
#ifdef __cplusplus
}
#endif
#endif
