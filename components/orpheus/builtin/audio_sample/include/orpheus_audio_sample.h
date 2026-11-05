#ifndef ORPHEUS_AUDIO_SAMPLE_H
#define ORPHEUS_AUDIO_SAMPLE_H
#include "orpheus_abi.h"
typedef struct { int channels, frames, frame; bool owns_memory; } AudioSampleState;
#endif
