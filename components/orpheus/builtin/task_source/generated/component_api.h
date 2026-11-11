#pragma once
#include "orpheus_abi.h"

/* input_index、drive_port_no、active 由壳填写，算法不得改写。
   count 组仅 [0, <接口名>_count) 有效；零上界数组保留一个无效占位。 */

typedef struct orpheus_builtin_task_source_InputF32 {
    const OrpheusBuffer* buffer;
    const float* samples;
    uint32_t input_index;
} orpheus_builtin_task_source_InputF32;
typedef struct orpheus_builtin_task_source_OutputF32 {
    OrpheusBuffer* buffer;
    float* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_task_source_OutputF32;

typedef struct orpheus_builtin_task_source_InputI32 {
    const OrpheusBuffer* buffer;
    const int32_t* samples;
    uint32_t input_index;
} orpheus_builtin_task_source_InputI32;
typedef struct orpheus_builtin_task_source_OutputI32 {
    OrpheusBuffer* buffer;
    int32_t* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_task_source_OutputI32;

typedef struct orpheus_builtin_task_source_InputI16 {
    const OrpheusBuffer* buffer;
    const int16_t* samples;
    uint32_t input_index;
} orpheus_builtin_task_source_InputI16;
typedef struct orpheus_builtin_task_source_OutputI16 {
    OrpheusBuffer* buffer;
    int16_t* samples;
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_task_source_OutputI16;

typedef struct orpheus_builtin_task_source_InputTrigger {
    const OrpheusDelivery* delivery;
    uint32_t input_index;
} orpheus_builtin_task_source_InputTrigger;
typedef struct orpheus_builtin_task_source_OutputTrigger {
    OrpheusDelivery delivery;
    uint32_t drive_port_no;
    bool active;
} orpheus_builtin_task_source_OutputTrigger;

#define ORPHEUS_BUILTIN_TASK_SOURCE_DRIVE_OUT UINT32_C(1)
typedef struct orpheus_builtin_task_source_IO {
    orpheus_builtin_task_source_OutputTrigger out;
} orpheus_builtin_task_source_IO;

static inline int orpheus_builtin_task_source_emit_out(const OrpheusProcessContext* ctx,
        const orpheus_builtin_task_source_OutputTrigger* output) {
    if (!ctx || !output || !output->active || output->delivery.buffer != NULL
            || output->drive_port_no < UINT32_C(1)
            || output->drive_port_no > UINT32_C(1))
        return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_emit(ctx, output->drive_port_no, &output->delivery);
}
