#ifndef ORPHEUS_PSD_H
#define ORPHEUS_PSD_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PSD_MAX_HALF 2048
#define PSD_MAX_CHANNELS 32

typedef struct {
    uint32_t channels;
    uint32_t half;
    float smoothing;       /* EMA 系数 1/smoothing_blocks */
    float* mags;           /* channels × half，平滑后幅度 */
    float* re;
    float* im;
    char json[65536];
} PsdState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_PSD_H */
