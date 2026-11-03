#ifndef ORPHEUS_FIR_BANK_H
#define ORPHEUS_FIR_BANK_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FIR_BANK_MAX_CHANNELS 64
#define FIR_BANK_MAX_TAPS 4096
#define FIR_BANK_MAX_COEFS 32768

typedef struct {
    uint32_t channels;
    uint32_t num_taps;
    uint32_t coefs_mode;
    int32_t processing_enabled;
    uint32_t taps_per_channel[FIR_BANK_MAX_CHANNELS];
    uint32_t positions[FIR_BANK_MAX_CHANNELS];
    float coefs[FIR_BANK_MAX_COEFS];
    float* history;
} FirBankState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif
#endif
