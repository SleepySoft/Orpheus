#ifndef ORPHEUS_LEVEL_DETECT_ALGORITHM_H
#define ORPHEUS_LEVEL_DETECT_ALGORITHM_H
typedef struct {
    float env[32],attack_coeff,release_coeff;
} orpheus_builtin_level_detect_AlgorithmState;
#endif
