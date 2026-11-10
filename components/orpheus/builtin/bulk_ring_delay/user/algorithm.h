#pragma once

typedef struct orpheus_builtin_bulk_ring_delay_AlgorithmState {
    float* history;
    size_t count;
    uint32_t cursor;
} orpheus_builtin_bulk_ring_delay_AlgorithmState;
