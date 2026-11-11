#ifndef ORPHEUS_BUILTIN_WAV_OUT_ALGORITHM_H
#define ORPHEUS_BUILTIN_WAV_OUT_ALGORITHM_H
#include <stdint.h>
#define ORPHEUS_WAV_OUT_CAPACITY (16u * 1024u * 1024u)
typedef struct orpheus_builtin_wav_out_AlgorithmState {
    float* samples;
    uint32_t count;
    uint32_t sample_rate;
} orpheus_builtin_wav_out_AlgorithmState;
#endif
