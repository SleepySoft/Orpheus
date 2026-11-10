#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_bulk_sum_State {
    int32_t channels;
    int32_t elements;
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_bulk_sum_AlgorithmState algorithm;
} orpheus_builtin_bulk_sum_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_bulk_sum_algorithm(orpheus_builtin_bulk_sum_State* state, orpheus_builtin_bulk_sum_IO* io, const OrpheusProcessContext* ctx);
#if defined(__cplusplus)
}
#endif
