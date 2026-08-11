#ifndef ORPHEUS_SPECTRAL_REDUCE_H
#define ORPHEUS_SPECTRAL_REDUCE_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t channels;
    uint32_t fft_size;
    uint32_t num_frames;
    uint32_t bin_count;
    uint32_t operation; /* 0=sum, 1=mean, 2=min, 3=max */
} SpectralReduceState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_SPECTRAL_REDUCE_H */
