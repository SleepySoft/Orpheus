#ifndef ORPHEUS_BULK_SLICE_H
#define ORPHEUS_BULK_SLICE_H
#include "orpheus_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t values[6];
    bool owns_memory;
} BulkSliceState;
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);
#ifdef __cplusplus
}
#endif
#endif
