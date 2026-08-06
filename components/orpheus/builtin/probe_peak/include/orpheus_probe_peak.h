#ifndef ORPHEUS_PROBE_PEAK_H
#define ORPHEUS_PROBE_PEAK_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float peak; uint32_t channels; } ProbePeakState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
