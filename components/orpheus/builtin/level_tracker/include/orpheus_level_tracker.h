#ifndef ORPHEUS_LEVEL_TRACKER_H
#define ORPHEUS_LEVEL_TRACKER_H
#include "orpheus_abi.h"
typedef struct {
    float rms_db, peak_db, prediction_db_spl, slow_decay_db, hold_margin_db, hold_calls;
    float fast_decay_db, min_db, max_db, volume_db, trigger, trigger_threshold, fast_multiplier, mode;
    float level_db_spl, envelope_db;
    float volume_epsilon;
    double hold_count;
    bool initialized, holding, owns_memory;
} LevelTrackerState;
#endif
