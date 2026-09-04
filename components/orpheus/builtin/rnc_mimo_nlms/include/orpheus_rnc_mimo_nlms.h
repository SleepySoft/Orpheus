#ifndef ORPHEUS_RNC_MIMO_NLMS_H
#define ORPHEUS_RNC_MIMO_NLMS_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RNC_NLMS_MAX_REFERENCES 12
#define RNC_NLMS_MAX_OUTPUTS 8
#define RNC_NLMS_MAX_TAPS 125
#define RNC_NLMS_MAX_WEIGHTS \
    (RNC_NLMS_MAX_REFERENCES * RNC_NLMS_MAX_OUTPUTS * RNC_NLMS_MAX_TAPS)
#define RNC_NLMS_MAX_HISTORY (RNC_NLMS_MAX_REFERENCES * RNC_NLMS_MAX_TAPS)

typedef struct {
    uint32_t reference_channels;
    uint32_t output_channels;
    uint32_t filter_length;
    uint32_t position;
    uint32_t leakage_reference;
    uint32_t leakage_output;
    float leakage;
    float eps;
    float step_sizes[RNC_NLMS_MAX_OUTPUTS];
    float initial_weights[RNC_NLMS_MAX_WEIGHTS];
    float weights[RNC_NLMS_MAX_WEIGHTS];
    float history[RNC_NLMS_MAX_HISTORY];
} RncMimoNlmsState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_RNC_MIMO_NLMS_H */
