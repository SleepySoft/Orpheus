#ifndef ORPHEUS_IFFT_H
#define ORPHEUS_IFFT_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IFFT_MAX_SIZE 1024

typedef struct {
    float twiddleCos[IFFT_MAX_SIZE / 2];
    float twiddleSin[IFFT_MAX_SIZE / 2];
    float scratchR[IFFT_MAX_SIZE];
    float scratchI[IFFT_MAX_SIZE];
    uint32_t fftSize;
    uint32_t channels;
} IfftState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_IFFT_H */
