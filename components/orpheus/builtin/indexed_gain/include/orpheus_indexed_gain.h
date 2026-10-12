#ifndef ORPHEUS_INDEXED_GAIN_H
#define ORPHEUS_INDEXED_GAIN_H
#include "orpheus_abi.h"
#define INDEXED_GAIN_MAX_CHANNELS 64
#define INDEXED_GAIN_MAX_TABLE 32
#define INDEXED_GAIN_SILENT 5.0118723e-7f
typedef struct {
    float table_idx[INDEXED_GAIN_MAX_TABLE];
    float table_db[INDEXED_GAIN_MAX_TABLE];
    uint32_t table_size;
    uint32_t channels;
    uint32_t block_size;
    float sample_rate;
    float gain_index;
    float gain_offset_db;
    float max_gain_db;
    float ramp_ms;
    float ramp_db_per_s;
    float current_gain;
    float target_gain;
    float ramp_coeff;
    uint32_t frame_count;
    int32_t freeze;
} IndexedGainState;
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);
#endif
