#pragma once
#include "orpheus_abi.h"

/* input_index、drive_port_no、active 由壳填写，算法不得改写。
   count 组仅 [0, <接口名>_count) 有效；零上界数组保留一个无效占位。 */

typedef struct orpheus_builtin_resample_InputF32 {
    const OrpheusBuffer* buffer;
    const float* samples;
    uint32_t input_index;
} orpheus_builtin_resample_InputF32;
typedef struct orpheus_builtin_resample_OutputF32 {
    OrpheusBuffer* buffer;
    float* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_resample_OutputF32;

typedef struct orpheus_builtin_resample_InputI32 {
    const OrpheusBuffer* buffer;
    const int32_t* samples;
    uint32_t input_index;
} orpheus_builtin_resample_InputI32;
typedef struct orpheus_builtin_resample_OutputI32 {
    OrpheusBuffer* buffer;
    int32_t* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_resample_OutputI32;

typedef struct orpheus_builtin_resample_InputI16 {
    const OrpheusBuffer* buffer;
    const int16_t* samples;
    uint32_t input_index;
} orpheus_builtin_resample_InputI16;
typedef struct orpheus_builtin_resample_OutputI16 {
    OrpheusBuffer* buffer;
    int16_t* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_resample_OutputI16;

typedef struct orpheus_builtin_resample_InputTrigger {
    const OrpheusDelivery* delivery;
    uint32_t input_index;
} orpheus_builtin_resample_InputTrigger;
typedef struct orpheus_builtin_resample_OutputTrigger {
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_resample_OutputTrigger;

enum {
    ORPHEUS_BUILTIN_RESAMPLE_INPUT_IN = 0,
    ORPHEUS_BUILTIN_RESAMPLE_INPUT_TICK = 1,
};
#define ORPHEUS_BUILTIN_RESAMPLE_DRIVE_OUT UINT32_C(1)
typedef struct orpheus_builtin_resample_IO {
    orpheus_builtin_resample_InputF32 in;
    orpheus_builtin_resample_InputTrigger tick;
    orpheus_builtin_resample_OutputF32 out;
} orpheus_builtin_resample_IO;

static inline int orpheus_builtin_resample_emit_out(const OrpheusProcessContext* ctx,
        const orpheus_builtin_resample_OutputF32* output) {
    if (!ctx || !output || !output->active || output->delivery.buffer != output->buffer
            || output->drive_port_no < UINT32_C(1)
            || output->drive_port_no > UINT32_C(1))
        return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_emit(ctx, output->drive_port_no, &output->delivery);
}
