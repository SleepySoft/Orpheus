#pragma once
#include "orpheus_abi.h"

/* input_index、drive_port_no、active 由壳填写，算法不得改写。
   count 组仅 [0, <接口名>_count) 有效；零上界数组保留一个无效占位。 */

/* 非音频载荷按 block_size，缺省继承 ctx；不改写 Task 量子。 */

typedef struct orpheus_builtin_level_tracker_InputF32 {
    const OrpheusBuffer* buffer;
    const float* samples;
    uint32_t input_index;
} orpheus_builtin_level_tracker_InputF32;
typedef struct orpheus_builtin_level_tracker_OutputF32 {
    OrpheusBuffer* buffer;
    float* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_level_tracker_OutputF32;

typedef struct orpheus_builtin_level_tracker_InputI32 {
    const OrpheusBuffer* buffer;
    const int32_t* samples;
    uint32_t input_index;
} orpheus_builtin_level_tracker_InputI32;
typedef struct orpheus_builtin_level_tracker_OutputI32 {
    OrpheusBuffer* buffer;
    int32_t* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_level_tracker_OutputI32;

typedef struct orpheus_builtin_level_tracker_InputI16 {
    const OrpheusBuffer* buffer;
    const int16_t* samples;
    uint32_t input_index;
} orpheus_builtin_level_tracker_InputI16;
typedef struct orpheus_builtin_level_tracker_OutputI16 {
    OrpheusBuffer* buffer;
    int16_t* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_level_tracker_OutputI16;

enum {
    ORPHEUS_BUILTIN_LEVEL_TRACKER_INPUT_RMS = 0,
    ORPHEUS_BUILTIN_LEVEL_TRACKER_INPUT_PEAK = 1,
    ORPHEUS_BUILTIN_LEVEL_TRACKER_INPUT_VOLUME = 2,
    ORPHEUS_BUILTIN_LEVEL_TRACKER_INPUT_FAST_TRIGGER = 3,
    ORPHEUS_BUILTIN_LEVEL_TRACKER_INPUT_VOLUME_GAIN = 4,
};
#define ORPHEUS_BUILTIN_LEVEL_TRACKER_DRIVE_LEVEL UINT32_C(1)
#define ORPHEUS_BUILTIN_LEVEL_TRACKER_DRIVE_ENVELOPE UINT32_C(2)
typedef struct orpheus_builtin_level_tracker_IO {
    orpheus_builtin_level_tracker_InputF32 rms;
    orpheus_builtin_level_tracker_InputF32 peak;
    orpheus_builtin_level_tracker_InputF32 volume;
    orpheus_builtin_level_tracker_InputF32 fast_trigger;
    orpheus_builtin_level_tracker_InputF32 volume_gain;
    orpheus_builtin_level_tracker_OutputF32 level;
    orpheus_builtin_level_tracker_OutputF32 envelope;
} orpheus_builtin_level_tracker_IO;

static inline int orpheus_builtin_level_tracker_emit_level(const OrpheusProcessContext* ctx,
        const orpheus_builtin_level_tracker_OutputF32* output) {
    if (!ctx || !output || !output->active || output->delivery.buffer != output->buffer
            || output->drive_port_no < UINT32_C(1)
            || output->drive_port_no > UINT32_C(1))
        return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_emit(ctx, output->drive_port_no, &output->delivery);
}
static inline int orpheus_builtin_level_tracker_emit_envelope(const OrpheusProcessContext* ctx,
        const orpheus_builtin_level_tracker_OutputF32* output) {
    if (!ctx || !output || !output->active || output->delivery.buffer != output->buffer
            || output->drive_port_no < UINT32_C(2)
            || output->drive_port_no > UINT32_C(2))
        return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_emit(ctx, output->drive_port_no, &output->delivery);
}
