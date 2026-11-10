#pragma once
#include "orpheus_abi.h"

typedef struct orpheus_builtin_saturation_InputF32 {
    const OrpheusBuffer* buffer;
    const float* samples;
} orpheus_builtin_saturation_InputF32;
typedef struct orpheus_builtin_saturation_OutputF32 {
    OrpheusBuffer* buffer;
    float* samples;
    OrpheusDelivery delivery;
} orpheus_builtin_saturation_OutputF32;

typedef struct orpheus_builtin_saturation_InputI32 {
    const OrpheusBuffer* buffer;
    const int32_t* samples;
} orpheus_builtin_saturation_InputI32;
typedef struct orpheus_builtin_saturation_OutputI32 {
    OrpheusBuffer* buffer;
    int32_t* samples;
    OrpheusDelivery delivery;
} orpheus_builtin_saturation_OutputI32;

typedef struct orpheus_builtin_saturation_InputI16 {
    const OrpheusBuffer* buffer;
    const int16_t* samples;
} orpheus_builtin_saturation_InputI16;
typedef struct orpheus_builtin_saturation_OutputI16 {
    OrpheusBuffer* buffer;
    int16_t* samples;
    OrpheusDelivery delivery;
} orpheus_builtin_saturation_OutputI16;

enum {
    ORPHEUS_BUILTIN_SATURATION_INPUT_IN = 0,
};
#define ORPHEUS_BUILTIN_SATURATION_DRIVE_OUT UINT32_C(1)
typedef struct orpheus_builtin_saturation_IO {
    orpheus_builtin_saturation_InputF32 in;
    orpheus_builtin_saturation_OutputF32 out;
} orpheus_builtin_saturation_IO;

static inline int orpheus_builtin_saturation_emit_out(const OrpheusProcessContext* ctx,
        const orpheus_builtin_saturation_OutputF32* output) {
    if (!output || output->delivery.buffer != output->buffer)
        return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_emit(ctx, ORPHEUS_BUILTIN_SATURATION_DRIVE_OUT, &output->delivery);
}
