#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_linear_gain_State {
    float gain;
    int32_t channels;
    orpheus_builtin_linear_gain_AlgorithmState algorithm;
} orpheus_builtin_linear_gain_State;

int orpheus_builtin_linear_gain_algorithm(orpheus_builtin_linear_gain_State* state, orpheus_builtin_linear_gain_IO* io, const OrpheusProcessContext* ctx);
