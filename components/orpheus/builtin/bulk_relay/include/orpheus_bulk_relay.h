#ifndef ORPHEUS_BULK_RELAY_H
#define ORPHEUS_BULK_RELAY_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORPHEUS_BULK_RELAY_CAPACITY 2048

typedef struct {
    float values[ORPHEUS_BULK_RELAY_CAPACITY];
    uint32_t count;
} BulkRelayState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif