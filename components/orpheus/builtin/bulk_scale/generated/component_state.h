#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_bulk_scale_State {
    int32_t channels;
    int32_t elements;
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_bulk_scale_AlgorithmState algorithm;
} orpheus_builtin_bulk_scale_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_bulk_scale_algorithm(orpheus_builtin_bulk_scale_State* state, orpheus_builtin_bulk_scale_IO* io, const OrpheusProcessContext* ctx);
#if defined(__cplusplus)
}
#endif
