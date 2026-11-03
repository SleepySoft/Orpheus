#ifndef ORPHEUS_BULK_SUM_H
#define ORPHEUS_BULK_SUM_H
#include "orpheus_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t channels,elements;
    bool owns_memory;
} BulkSumState;
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);
#ifdef __cplusplus
}
#endif
#endif
