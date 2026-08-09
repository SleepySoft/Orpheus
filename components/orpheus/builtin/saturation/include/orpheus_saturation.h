#ifndef ORPHEUS_SATURATION_H
#define ORPHEUS_SATURATION_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float limit;
    float soft;
    uint32_t channels;
} SaturationState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_SATURATION_H */
