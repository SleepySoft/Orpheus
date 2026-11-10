#pragma once
#include "orpheus_abi.h"

/* input_index、drive_port_no、active 由壳填写，算法不得改写。
   count 组仅 [0, <接口名>_count) 有效；零上界数组保留一个无效占位。 */

/* 非音频载荷按 block_size，缺省继承 ctx；不改写 Task 量子。 */

typedef struct orpheus_builtin_bulk_constant_InputF32 {
    const OrpheusBuffer* buffer;
    const float* samples;
    uint32_t input_index;
} orpheus_builtin_bulk_constant_InputF32;
typedef struct orpheus_builtin_bulk_constant_OutputF32 {
    OrpheusBuffer* buffer;
    float* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_bulk_constant_OutputF32;

typedef struct orpheus_builtin_bulk_constant_InputI32 {
    const OrpheusBuffer* buffer;
    const int32_t* samples;
    uint32_t input_index;
} orpheus_builtin_bulk_constant_InputI32;
typedef struct orpheus_builtin_bulk_constant_OutputI32 {
    OrpheusBuffer* buffer;
    int32_t* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_bulk_constant_OutputI32;

typedef struct orpheus_builtin_bulk_constant_InputI16 {
    const OrpheusBuffer* buffer;
    const int16_t* samples;
    uint32_t input_index;
} orpheus_builtin_bulk_constant_InputI16;
typedef struct orpheus_builtin_bulk_constant_OutputI16 {
    OrpheusBuffer* buffer;
    int16_t* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_bulk_constant_OutputI16;

typedef struct orpheus_builtin_bulk_constant_InputTrigger {
    const OrpheusDelivery* delivery;
    uint32_t input_index;
} orpheus_builtin_bulk_constant_InputTrigger;
typedef struct orpheus_builtin_bulk_constant_OutputTrigger {
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_bulk_constant_OutputTrigger;

enum {
    ORPHEUS_BUILTIN_BULK_CONSTANT_INPUT_TICK = 0,
};
#define ORPHEUS_BUILTIN_BULK_CONSTANT_DRIVE_OUT UINT32_C(1)
#define ORPHEUS_BUILTIN_BULK_CONSTANT_DRIVE_FIRST UINT32_C(2)
#define ORPHEUS_BUILTIN_BULK_CONSTANT_DRIVE_LAST UINT32_C(3)
typedef struct orpheus_builtin_bulk_constant_IO {
    orpheus_builtin_bulk_constant_InputTrigger tick;
    orpheus_builtin_bulk_constant_OutputF32 out;
    orpheus_builtin_bulk_constant_OutputF32 first;
    orpheus_builtin_bulk_constant_OutputF32 last;
} orpheus_builtin_bulk_constant_IO;

static inline int orpheus_builtin_bulk_constant_emit_out(const OrpheusProcessContext* ctx,
        const orpheus_builtin_bulk_constant_OutputF32* output) {
    if (!ctx || !output || !output->active || output->delivery.buffer != output->buffer
            || output->drive_port_no < UINT32_C(1)
            || output->drive_port_no > UINT32_C(1))
        return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_emit(ctx, output->drive_port_no, &output->delivery);
}
static inline int orpheus_builtin_bulk_constant_emit_first(const OrpheusProcessContext* ctx,
        const orpheus_builtin_bulk_constant_OutputF32* output) {
    if (!ctx || !output || !output->active || output->delivery.buffer != output->buffer
            || output->drive_port_no < UINT32_C(2)
            || output->drive_port_no > UINT32_C(2))
        return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_emit(ctx, output->drive_port_no, &output->delivery);
}
static inline int orpheus_builtin_bulk_constant_emit_last(const OrpheusProcessContext* ctx,
        const orpheus_builtin_bulk_constant_OutputF32* output) {
    if (!ctx || !output || !output->active || output->delivery.buffer != output->buffer
            || output->drive_port_no < UINT32_C(3)
            || output->drive_port_no > UINT32_C(3))
        return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_emit(ctx, output->drive_port_no, &output->delivery);
}
