#ifndef ORPHEUS_SWEEP_RECORD_ALGORITHM_H
#define ORPHEUS_SWEEP_RECORD_ALGORITHM_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    double t;
    uint64_t total_frames;
    uint64_t duration_frames;
    uint64_t quiet_frames;
    uint64_t count[256];
    float acc[256];
    float peak_rms;
    double sample_rate;
    bool done;
} orpheus_builtin_sweep_record_AlgorithmState;
#endif
