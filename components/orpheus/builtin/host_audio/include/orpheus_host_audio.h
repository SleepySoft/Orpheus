#ifndef ORPHEUS_HOST_AUDIO_H
#define ORPHEUS_HOST_AUDIO_H
#include "orpheus_abi.h"
#include "orpheus_chain.h"
#define ORPHEUS_HOST_AUDIO_CAPACITY 8192
typedef struct {
    float data[ORPHEUS_HOST_AUDIO_CAPACITY];
    uint32_t channels, frames, mode;
    int32_t missing_blocks;
    bool ready, valid, owns_memory;
} HostAudioState;
#endif
