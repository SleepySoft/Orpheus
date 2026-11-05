#ifndef ORPHEUS_BULK_LOOKUP_H
#define ORPHEUS_BULK_LOOKUP_H
#include "orpheus_abi.h"
typedef struct {
    uint32_t points;
    bool owns_memory;
} BulkLookupState;
#endif
