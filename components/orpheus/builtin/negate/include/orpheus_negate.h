#ifndef ORPHEUS_NEGATE_H
#define ORPHEUS_NEGATE_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEG_MAX_CH 64

typedef struct {
    uint32_t channels;
} NegateState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
