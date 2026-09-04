#ifndef ORPHEUS_BAF_SOFT_CLIPPER_H
#define ORPHEUS_BAF_SOFT_CLIPPER_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float xmin;
    float xmax;
    float p2;
    float xmin_low;
    float xmax_low;
    float p2_low;
    uint32_t channels;
    uint32_t param_set;
    bool disabled;
    int32_t active_mask;
} BafSoftClipperState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_BAF_SOFT_CLIPPER_H */
