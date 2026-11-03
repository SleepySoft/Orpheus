#ifndef ORPHEUS_BULK_RING_DELAY_H
#define ORPHEUS_BULK_RING_DELAY_H
#include "orpheus_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t channels,bins,batches,capacity_elements,delay_elements,cursor;
    float* history;
    bool owns_memory;
} BulkRingDelayState;
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);
#ifdef __cplusplus
}
#endif
#endif
