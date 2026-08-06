#ifndef ORPHEUS_PROBE_RMS_H
#define ORPHEUS_PROBE_RMS_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float rms;               /* 探针值（注册槽，组件实时写 / Runtime 读） */
    uint32_t channels;
} ProbeRmsState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
