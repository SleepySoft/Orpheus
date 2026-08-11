#ifndef ORPHEUS_WINDOW_H
#define ORPHEUS_WINDOW_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORPHEUS_WINDOW_MAX 4096

typedef struct {
    uint32_t window_size;
    uint32_t channels;
    uint32_t mode;                /* 0=single, 1=repeat */
    float coeffs[ORPHEUS_WINDOW_MAX];
} WindowState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_WINDOW_H */
