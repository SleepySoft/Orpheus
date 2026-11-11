#ifndef ORPHEUS_WAV_IN_ALGORITHM_H
#define ORPHEUS_WAV_IN_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float* samples;
    uint32_t total_frames,position;
} orpheus_builtin_wav_in_AlgorithmState;
#endif
