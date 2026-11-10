#pragma once
#include "orpheus_abi.h"

typedef struct orpheus_builtin_null_sink_InputF32 {
    const OrpheusBuffer* buffer;
    const float* samples;
} orpheus_builtin_null_sink_InputF32;
typedef struct orpheus_builtin_null_sink_OutputF32 {
    OrpheusBuffer* buffer;
    float* samples;
    OrpheusDelivery delivery;
} orpheus_builtin_null_sink_OutputF32;

enum {
    ORPHEUS_BUILTIN_NULL_SINK_INPUT_IN = 0,
};
typedef struct orpheus_builtin_null_sink_IO {
    orpheus_builtin_null_sink_InputF32 in;
} orpheus_builtin_null_sink_IO;
