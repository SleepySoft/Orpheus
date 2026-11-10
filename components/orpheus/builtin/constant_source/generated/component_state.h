#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_constant_source_State {
    float value;
    int32_t channels;
#if defined(__cplusplus)
    alignas(1) bool _orpheus_alive;
#else
    _Alignas(1) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_constant_source_AlgorithmState algorithm;
} orpheus_builtin_constant_source_State;

int orpheus_builtin_constant_source_algorithm(orpheus_builtin_constant_source_State* state, orpheus_builtin_constant_source_IO* io, const OrpheusProcessContext* ctx);
