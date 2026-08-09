#ifndef ORPHEUS_SOFT_CLIPPER_H
#define ORPHEUS_SOFT_CLIPPER_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float drive_db;
    float drive_linear;
    float norm;          /* 1/tanh(drive_linear)，保持小信号增益 ≈1 */
    uint32_t channels;
} SoftClipperState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_SOFT_CLIPPER_H */
