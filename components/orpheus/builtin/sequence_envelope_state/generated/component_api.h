#pragma once
#include "orpheus_abi.h"

/* input_index、drive_port_no、active 由壳填写，算法不得改写。
   count 组仅 [0, <接口名>_count) 有效；零上界数组保留一个无效占位。 */

/* 非音频载荷按 block_size，缺省继承 ctx；不改写 Task 量子。 */

typedef struct orpheus_builtin_sequence_envelope_state_InputF32 {
    const OrpheusBuffer* buffer;
    const float* samples;
    uint32_t input_index;
} orpheus_builtin_sequence_envelope_state_InputF32;
typedef struct orpheus_builtin_sequence_envelope_state_OutputF32 {
    OrpheusBuffer* buffer;
    float* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_sequence_envelope_state_OutputF32;

typedef struct orpheus_builtin_sequence_envelope_state_InputI32 {
    const OrpheusBuffer* buffer;
    const int32_t* samples;
    uint32_t input_index;
} orpheus_builtin_sequence_envelope_state_InputI32;
typedef struct orpheus_builtin_sequence_envelope_state_OutputI32 {
    OrpheusBuffer* buffer;
    int32_t* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_sequence_envelope_state_OutputI32;

typedef struct orpheus_builtin_sequence_envelope_state_InputI16 {
    const OrpheusBuffer* buffer;
    const int16_t* samples;
    uint32_t input_index;
} orpheus_builtin_sequence_envelope_state_InputI16;
typedef struct orpheus_builtin_sequence_envelope_state_OutputI16 {
    OrpheusBuffer* buffer;
    int16_t* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_sequence_envelope_state_OutputI16;

enum {
    ORPHEUS_BUILTIN_SEQUENCE_ENVELOPE_STATE_INPUT_TARGETS = 0,
    ORPHEUS_BUILTIN_SEQUENCE_ENVELOPE_STATE_INPUT_SEQUENCE = 1,
    ORPHEUS_BUILTIN_SEQUENCE_ENVELOPE_STATE_INPUT_COEFFICIENT = 2,
    ORPHEUS_BUILTIN_SEQUENCE_ENVELOPE_STATE_INPUT_FEEDBACK = 3,
};
#define ORPHEUS_BUILTIN_SEQUENCE_ENVELOPE_STATE_DRIVE_START UINT32_C(1)
#define ORPHEUS_BUILTIN_SEQUENCE_ENVELOPE_STATE_DRIVE_ACCEPTED UINT32_C(2)
#define ORPHEUS_BUILTIN_SEQUENCE_ENVELOPE_STATE_DRIVE_ALPHA UINT32_C(3)
#define ORPHEUS_BUILTIN_SEQUENCE_ENVELOPE_STATE_DRIVE_FREEZE UINT32_C(4)
typedef struct orpheus_builtin_sequence_envelope_state_IO {
    orpheus_builtin_sequence_envelope_state_InputF32 targets;
    orpheus_builtin_sequence_envelope_state_InputI32 sequence;
    orpheus_builtin_sequence_envelope_state_InputF32 coefficient;
    orpheus_builtin_sequence_envelope_state_InputF32 feedback;
    orpheus_builtin_sequence_envelope_state_OutputF32 start;
    orpheus_builtin_sequence_envelope_state_OutputF32 accepted;
    orpheus_builtin_sequence_envelope_state_OutputF32 alpha;
    orpheus_builtin_sequence_envelope_state_OutputI32 freeze;
} orpheus_builtin_sequence_envelope_state_IO;

static inline int orpheus_builtin_sequence_envelope_state_emit_start(const OrpheusProcessContext* ctx,
        const orpheus_builtin_sequence_envelope_state_OutputF32* output) {
    if (!ctx || !output || !output->active || output->delivery.buffer != output->buffer
            || output->drive_port_no < UINT32_C(1)
            || output->drive_port_no > UINT32_C(1))
        return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_emit(ctx, output->drive_port_no, &output->delivery);
}
static inline int orpheus_builtin_sequence_envelope_state_emit_accepted(const OrpheusProcessContext* ctx,
        const orpheus_builtin_sequence_envelope_state_OutputF32* output) {
    if (!ctx || !output || !output->active || output->delivery.buffer != output->buffer
            || output->drive_port_no < UINT32_C(2)
            || output->drive_port_no > UINT32_C(2))
        return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_emit(ctx, output->drive_port_no, &output->delivery);
}
static inline int orpheus_builtin_sequence_envelope_state_emit_alpha(const OrpheusProcessContext* ctx,
        const orpheus_builtin_sequence_envelope_state_OutputF32* output) {
    if (!ctx || !output || !output->active || output->delivery.buffer != output->buffer
            || output->drive_port_no < UINT32_C(3)
            || output->drive_port_no > UINT32_C(3))
        return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_emit(ctx, output->drive_port_no, &output->delivery);
}
static inline int orpheus_builtin_sequence_envelope_state_emit_freeze(const OrpheusProcessContext* ctx,
        const orpheus_builtin_sequence_envelope_state_OutputI32* output) {
    if (!ctx || !output || !output->active || output->delivery.buffer != output->buffer
            || output->drive_port_no < UINT32_C(4)
            || output->drive_port_no > UINT32_C(4))
        return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_emit(ctx, output->drive_port_no, &output->delivery);
}
