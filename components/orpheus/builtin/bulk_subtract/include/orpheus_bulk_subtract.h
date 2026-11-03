#ifndef ORPHEUS_BULK_SUBTRACT_H
#define ORPHEUS_BULK_SUBTRACT_H
#include "orpheus_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t channels,elements;
    bool owns_memory;
} BulkSubtractState;
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);
#ifdef __cplusplus
}
#endif
#endif
