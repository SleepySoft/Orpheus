#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_one_pole_envelope_State {
    int32_t channels;
#if defined(__cplusplus)
    alignas(1) bool _orpheus_alive;
#else
    _Alignas(1) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_one_pole_envelope_AlgorithmState algorithm;
} orpheus_builtin_one_pole_envelope_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_one_pole_envelope_algorithm(orpheus_builtin_one_pole_envelope_State* state, orpheus_builtin_one_pole_envelope_IO* io, const OrpheusProcessContext* ctx);
#if defined(__cplusplus)
}
#endif
