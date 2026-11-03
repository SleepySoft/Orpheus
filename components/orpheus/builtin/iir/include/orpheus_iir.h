#ifndef ORPHEUS_IIR_H
#define ORPHEUS_IIR_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IIR_MAX_CHANNELS 64
#define IIR_MAX_ORDER 16
#define IIR_COEF_STRIDE (2 * IIR_MAX_ORDER + 1)

typedef struct {
    float coefs[IIR_MAX_CHANNELS][IIR_COEF_STRIDE];
    float history[IIR_MAX_CHANNELS][IIR_MAX_ORDER];
    uint32_t channels;
    uint32_t order;
    uint32_t coefs_mode;
    int32_t processing_enabled;
} IirState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
