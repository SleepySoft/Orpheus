#ifndef ORPHEUS_BUILTIN_SPATIAL_ENHANCER_ALGORITHM_H
#define ORPHEUS_BUILTIN_SPATIAL_ENHANCER_ALGORITHM_H
typedef struct orpheus_builtin_spatial_enhancer_AlgorithmState {
    float width_smoothed;
    float air_smoothed;
    float mono_mix_smoothed;
    float mono_coeff;
    float hp_prev_side;
    float hp_prev_out;
    float hp_fb;
    float hp_gain;
} orpheus_builtin_spatial_enhancer_AlgorithmState;
#endif
