#ifndef ORPHEUS_SLEEPING_BEAUTY_ALGORITHM_H
#define ORPHEUS_SLEEPING_BEAUTY_ALGORITHM_H
#include <stdbool.h>
#include <stdint.h>
#define SB_SILENT_GAIN 5.0118723e-7f
typedef struct {
    float current_gain,target_gain,coefficient;
    uint32_t remaining;
} SBRamper;
typedef struct {
    SBRamper rampers[4];
    float table_idx[30],table_db[30];
    uint32_t table_size;
    int32_t channel_map[64];
    float sample_rate;
    uint32_t block_size;
    float last_index,last_ms,last_rate;
    bool controls_dirty;
} orpheus_builtin_sleeping_beauty_AlgorithmState;
#endif
