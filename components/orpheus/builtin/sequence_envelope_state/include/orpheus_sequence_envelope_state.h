#ifndef ORPHEUS_SEQUENCE_ENVELOPE_STATE_H
#define ORPHEUS_SEQUENCE_ENVELOPE_STATE_H
#include "orpheus_abi.h"
typedef struct {
    bool owns_memory, initialized;
    uint32_t sequence;
    float current[128], targets[128], coefficient, silent;
    int32_t channels;
} SequenceEnvelopeState;
#endif
