#pragma once

typedef struct orpheus_builtin_sequence_envelope_state_AlgorithmState {
    bool initialized;
    uint32_t sequence;
    float current[128], targets[128], coefficient;
} orpheus_builtin_sequence_envelope_state_AlgorithmState;
