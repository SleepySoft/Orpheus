#ifndef ORPHEUS_BUILTIN_MIXER_ALGORITHM_H
#define ORPHEUS_BUILTIN_MIXER_ALGORITHM_H
typedef struct orpheus_builtin_mixer_AlgorithmState {
    float gain_linear[2];
    float target_gain_linear[2];
} orpheus_builtin_mixer_AlgorithmState;
#endif
