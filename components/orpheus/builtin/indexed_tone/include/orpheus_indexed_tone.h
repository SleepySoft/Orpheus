#ifndef ORPHEUS_INDEXED_TONE_H
#define ORPHEUS_INDEXED_TONE_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INDEXED_TONE_MAX_CHANNELS 64
#define INDEXED_TONE_COEFS 5
#define INDEXED_TONE_SILENT_GAIN 5.0118723e-7f

typedef struct {
    float boost_coefs[INDEXED_TONE_COEFS];
    float cut_coefs[INDEXED_TONE_COEFS];
    float z1[INDEXED_TONE_MAX_CHANNELS];
    float z2[INDEXED_TONE_MAX_CHANNELS];
    float gain_index;
    float offset;
    float scale;
    float max_boost_db;
    float max_cut_db;
    float ramp_ms;
    float ramp_db_per_s;
    float current_gain;
    float target_gain;
    float final_target_gain;
    float ramp_coeff;
    uint32_t frame_count;
    uint32_t channels;
    uint32_t block_size;
    float sample_rate;
    int32_t filter_order;
    int32_t freeze;
    int32_t crossing_phase;
} IndexedToneState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
