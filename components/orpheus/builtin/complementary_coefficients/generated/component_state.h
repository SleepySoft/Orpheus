#pragma once
#include "component_api.h"
#include "../user/algorithm.h"

typedef struct orpheus_builtin_complementary_coefficients_State {
    int32_t channels;
    int32_t elements;
    float total;
#if defined(__cplusplus)
    alignas(8) bool _orpheus_alive;
#else
    _Alignas(8) bool _orpheus_alive;
#endif
    bool _orpheus_cleanup_pending;
    orpheus_builtin_complementary_coefficients_AlgorithmState algorithm;
} orpheus_builtin_complementary_coefficients_State;

#if defined(__cplusplus)
extern "C" {
#endif
int orpheus_builtin_complementary_coefficients_algorithm(orpheus_builtin_complementary_coefficients_State* state, orpheus_builtin_complementary_coefficients_IO* io, const OrpheusProcessContext* ctx);
#if defined(__cplusplus)
}
#endif
