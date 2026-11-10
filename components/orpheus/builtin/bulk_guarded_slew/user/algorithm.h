#pragma once

typedef struct orpheus_builtin_bulk_guarded_slew_History {
    float value, delta, change;
} orpheus_builtin_bulk_guarded_slew_History;

typedef struct orpheus_builtin_bulk_guarded_slew_AlgorithmState {
    orpheus_builtin_bulk_guarded_slew_History* history;
    size_t count;
    bool initialized;
} orpheus_builtin_bulk_guarded_slew_AlgorithmState;
