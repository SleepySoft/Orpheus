#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_null_sink_State {
    int32_t channels;
    orpheus_builtin_null_sink_AlgorithmState algorithm;
} orpheus_builtin_null_sink_State;

int orpheus_builtin_null_sink_algorithm(orpheus_builtin_null_sink_State* state, orpheus_builtin_null_sink_IO* io, const OrpheusProcessContext* ctx);
