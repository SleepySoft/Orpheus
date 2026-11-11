#ifndef ORPHEUS_BUILTIN_SINE_MOD_ALGORITHM_H
#define ORPHEUS_BUILTIN_SINE_MOD_ALGORITHM_H
typedef struct orpheus_builtin_sine_mod_AlgorithmState {
    float phase;
    float phase_inc;
    float sample_rate;
    unsigned table_index;
    float carrier_table[1024];
} orpheus_builtin_sine_mod_AlgorithmState;
#endif
