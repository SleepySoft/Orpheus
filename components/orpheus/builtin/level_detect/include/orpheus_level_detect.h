#ifndef ORPHEUS_LEVEL_DETECT_H
#define ORPHEUS_LEVEL_DETECT_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t mode;        /* 0=峰值 1=RMS */
    float attack_ms;
    float release_ms;
    float attack_coeff;
    float release_coeff;
    float env[32];
    float level;         /* 探针：各通道包络最大值 */
    uint32_t channels;
} LevelDetectState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_LEVEL_DETECT_H */
