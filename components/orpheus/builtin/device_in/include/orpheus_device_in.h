#ifndef ORPHEUS_DEVICE_IN_H
#define ORPHEUS_DEVICE_IN_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint32_t channels; } DeviceInState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
