#ifndef ORPHEUS_MAPPED_FIR_BANK_H
#define ORPHEUS_MAPPED_FIR_BANK_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAPPED_FIR_MAX_INPUTS 64
#define MAPPED_FIR_MAX_FILTERS 64
#define MAPPED_FIR_MAX_OUTPUTS 64
#define MAPPED_FIR_MAX_TAPS 4096

typedef struct {
    uint32_t input_channels;
    uint32_t filter_count;
    uint32_t output_channels;
    uint32_t max_taps;
    uint32_t coefficient_count;
    uint32_t output_delay_samples;
    uint32_t output_delay_position;
    uint32_t filter_lengths[MAPPED_FIR_MAX_FILTERS];
    uint32_t coefficient_mapping[MAPPED_FIR_MAX_FILTERS];
    uint32_t coefficient_offsets[MAPPED_FIR_MAX_FILTERS];
    uint32_t input_mapping[MAPPED_FIR_MAX_FILTERS];
    uint32_t output_starts[MAPPED_FIR_MAX_OUTPUTS];
    uint32_t positions[MAPPED_FIR_MAX_INPUTS];
    float* coefficients;
    float* history;
    float* output_delay;
} MappedFirBankState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif