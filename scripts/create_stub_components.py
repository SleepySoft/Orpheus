#!/usr/bin/env python3
"""Generate minimal stub components for the foundational version."""

import os
from pathlib import Path

BASE = Path(__file__).parent.parent / "components" / "orpheus" / "builtin"


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    print(f"wrote {path}")


HEADER_TEMPLATE = """#ifndef ORPHEUS_{NAME_UPPER}_H
#define ORPHEUS_{NAME_UPPER}_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {{
#endif

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}}
#endif

#endif
"""

CMAKE_TEMPLATE = """cmake_minimum_required(VERSION 3.16)

set(COMPONENT_NAME orpheus_builtin_{name})

add_library(${{COMPONENT_NAME}} SHARED)

target_sources(${{COMPONENT_NAME}}
    PRIVATE
        src/{name}.c
)

target_include_directories(${{COMPONENT_NAME}}
    PUBLIC
        ${{CMAKE_CURRENT_SOURCE_DIR}}/include
        ${{CMAKE_SOURCE_DIR}}/orpheus_abi/include
)

target_compile_definitions(${{COMPONENT_NAME}}
    PRIVATE
        ORPHEUS_BUILDING_COMPONENT
)

set_target_properties(${{COMPONENT_NAME}} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${{CMAKE_BINARY_DIR}}/components
    LIBRARY_OUTPUT_DIRECTORY ${{CMAKE_BINARY_DIR}}/components
    ARCHIVE_OUTPUT_DIRECTORY ${{CMAKE_BINARY_DIR}}/components
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)
"""


def create_split():
    comp_dir = BASE / "split"
    write(comp_dir / "component.yaml", """id: orpheus.builtin.split
version: 1.0.0
abi_version: 1
package_type: source
sources:
  - src/split.c
headers:
  - include/orpheus_split.h
ports:
  - id: in
    direction: input
    type: audio
    sample_format: f32
    channels: param:channels
  - id: out0
    direction: output
    type: audio
    sample_format: f32
    channels: param:channels
  - id: out1
    direction: output
    type: audio
    sample_format: f32
    channels: param:channels
parameters:
  - id: channels
    name: Channels
    type: int
    default: 2
    range: [1, 32]
    update_policy: restart_required
    affects_signature: true
memory:
  state_size: 0
  scratch_size: 0
  alignment: 8
execution:
  sample_rate_independent: true
  latency_samples: 0
  supports_inplace: false
  realtime_safe: true
""")
    write(comp_dir / "include" / "orpheus_split.h", HEADER_TEMPLATE.format(NAME_UPPER="SPLIT"))
    write(comp_dir / "CMakeLists.txt", CMAKE_TEMPLATE.format(name="split"))
    write(comp_dir / "src" / "split.c", """#include "orpheus_split.h"

#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t channels; } SplitState;

static const OrpheusParameter params[] = {
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort ports[] = {
    { "in", ORPHEUS_PORT_INPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "out0", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "out1", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.split", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 3, params, 1, sizeof(SplitState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(SplitState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { free(state); return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    SplitState* s = (SplitState*)state; s->channels = config->channels > 0 ? config->channels : 2; return ORPHEUS_OK;
}
static int reset(void* state) { (void)state; return ORPHEUS_OK; }
static int process(void* state, const OrpheusProcessContext* ctx) {
    SplitState* s = (SplitState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out0 = ctx->outputs[0];
    OrpheusBuffer* out1 = ctx->outputs[1];
    if (!in || !out0 || !out1) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n = ctx->frame_count * s->channels;
    memcpy(out0->data, in->data, n * sizeof(float));
    memcpy(out1->data, in->data, n * sizeof(float));
    out0->frame_count = ctx->frame_count; out1->frame_count = ctx->frame_count;
    return ORPHEUS_OK;
}
static int set_param(void* state, const char* id, const OrpheusValue* v) { (void)state; (void)id; (void)v; return ORPHEUS_ERR_UNSUPPORTED; }
static int get_param(void* state, const char* id, OrpheusValue* v) {
    SplitState* s = (SplitState*)state;
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL
};
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void) { return &iface; }
""")


def create_merge():
    comp_dir = BASE / "merge"
    write(comp_dir / "component.yaml", """id: orpheus.builtin.merge
version: 1.0.0
abi_version: 1
package_type: source
sources:
  - src/merge.c
headers:
  - include/orpheus_merge.h
ports:
  - id: in0
    direction: input
    type: audio
    sample_format: f32
    channels: param:channels
  - id: in1
    direction: input
    type: audio
    sample_format: f32
    channels: param:channels
  - id: out
    direction: output
    type: audio
    sample_format: f32
    channels: param:channels
parameters:
  - id: channels
    name: Channels
    type: int
    default: 2
    range: [1, 32]
    update_policy: restart_required
    affects_signature: true
memory:
  state_size: 0
  scratch_size: 0
  alignment: 8
execution:
  sample_rate_independent: true
  latency_samples: 0
  supports_inplace: false
  realtime_safe: true
""")
    write(comp_dir / "include" / "orpheus_merge.h", HEADER_TEMPLATE.format(NAME_UPPER="MERGE"))
    write(comp_dir / "CMakeLists.txt", CMAKE_TEMPLATE.format(name="merge"))
    write(comp_dir / "src" / "merge.c", """#include "orpheus_merge.h"

#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t channels; } MergeState;

static const OrpheusParameter params[] = {
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort ports[] = {
    { "in0", ORPHEUS_PORT_INPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "in1", ORPHEUS_PORT_INPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "out", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.merge", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 3, params, 1, sizeof(MergeState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(MergeState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { free(state); return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    MergeState* s = (MergeState*)state; s->channels = config->channels > 0 ? config->channels : 2; return ORPHEUS_OK;
}
static int reset(void* state) { (void)state; return ORPHEUS_OK; }
static int process(void* state, const OrpheusProcessContext* ctx) {
    MergeState* s = (MergeState*)state;
    const OrpheusBuffer* in0 = ctx->inputs[0];
    const OrpheusBuffer* in1 = ctx->inputs[1];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!in0 || !in1 || !out) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n = ctx->frame_count * s->channels;
    const float* a = (const float*)in0->data;
    const float* b = (const float*)in1->data;
    float* c = (float*)out->data;
    for (uint32_t i = 0; i < n; ++i) c[i] = a[i] + b[i];
    out->frame_count = ctx->frame_count;
    return ORPHEUS_OK;
}
static int set_param(void* state, const char* id, const OrpheusValue* v) { (void)state; (void)id; (void)v; return ORPHEUS_ERR_UNSUPPORTED; }
static int get_param(void* state, const char* id, OrpheusValue* v) {
    MergeState* s = (MergeState*)state;
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL
};
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void) { return &iface; }
""")


def create_delay():
    comp_dir = BASE / "delay"
    write(comp_dir / "component.yaml", """id: orpheus.builtin.delay
version: 1.0.0
abi_version: 1
package_type: source
sources:
  - src/delay.c
headers:
  - include/orpheus_delay.h
ports:
  - id: in
    direction: input
    type: audio
    sample_format: f32
    channels: param:channels
  - id: out
    direction: output
    type: audio
    sample_format: f32
    channels: param:channels
parameters:
  - id: delay_ms
    name: Delay Time
    type: float
    default: 100.0
    range: [0.0, 5000.0]
    unit: ms
    update_policy: restart_required
  - id: mix
    name: Mix
    type: float
    default: 0.5
    range: [0.0, 1.0]
    update_policy: smoothed
  - id: channels
    name: Channels
    type: int
    default: 2
    range: [1, 32]
    update_policy: restart_required
    affects_signature: true
memory:
  state_size: 0
  scratch_size: 0
  alignment: 8
execution:
  sample_rate_independent: false
  latency_samples: 0
  supports_inplace: false
  realtime_safe: true
""")
    write(comp_dir / "include" / "orpheus_delay.h", HEADER_TEMPLATE.format(NAME_UPPER="DELAY"))
    write(comp_dir / "CMakeLists.txt", CMAKE_TEMPLATE.format(name="delay"))
    write(comp_dir / "src" / "delay.c", """#include "orpheus_delay.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    float* buffer;
    uint32_t delay_samples;
    uint32_t write_pos;
    uint32_t channels;
    uint32_t capacity;
    float mix;
} DelayState;

static const OrpheusParameter params[] = {
    { .id = "delay_ms", .name = "Delay Time", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 100.0f },
      .min_f32 = 0.0f, .max_f32 = 5000.0f, .unit = "ms",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .readback = true, .persistent = true },
    { .id = "mix", .name = "Mix", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.5f },
      .min_f32 = 0.0f, .max_f32 = 1.0f,
      .update_policy = ORPHEUS_UPDATE_SMOOTHED, .readback = true, .persistent = true },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort ports[] = {
    { "in", ORPHEUS_PORT_INPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "out", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.delay", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 2, params, 3, sizeof(DelayState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(DelayState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) {
    DelayState* s = (DelayState*)state;
    if (s->buffer) free(s->buffer);
    free(s); return ORPHEUS_OK;
}
static int prepare(void* state, const OrpheusConfig* config) {
    DelayState* s = (DelayState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    float delay_ms = 100.0f;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "delay_ms") == 0 && config->param_values[i].type == ORPHEUS_VALUE_FLOAT)
            delay_ms = config->param_values[i].value.f32;
    }
    s->delay_samples = (uint32_t)((delay_ms / 1000.0f) * config->sample_rate + 0.5f);
    s->capacity = s->delay_samples * s->channels + 1024;
    s->buffer = (float*)calloc(s->capacity, sizeof(float));
    if (!s->buffer) return ORPHEUS_ERR_OUT_OF_MEMORY;
    s->write_pos = 0;
    s->mix = 0.5f;
    return ORPHEUS_OK;
}
static int reset(void* state) {
    DelayState* s = (DelayState*)state;
    if (s->buffer) memset(s->buffer, 0, s->capacity * sizeof(float));
    s->write_pos = 0; return ORPHEUS_OK;
}
static int process(void* state, const OrpheusProcessContext* ctx) {
    DelayState* s = (DelayState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!in || !out) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    for (uint32_t n = 0; n < frames; ++n) {
        for (uint32_t c = 0; c < ch; ++c) {
            uint32_t idx = (s->write_pos + s->capacity - s->delay_samples * ch) % s->capacity;
            float delayed = s->buffer[idx + c];
            float x = in_data[n * ch + c];
            s->buffer[s->write_pos + c] = x;
            out_data[n * ch + c] = x + s->mix * delayed;
        }
        s->write_pos = (s->write_pos + ch) % s->capacity;
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int set_param(void* state, const char* id, const OrpheusValue* v) {
    DelayState* s = (DelayState*)state;
    if (strcmp(id, "mix") == 0 && v->type == ORPHEUS_VALUE_FLOAT) { s->mix = v->value.f32; return ORPHEUS_OK; }
    return ORPHEUS_ERR_UNSUPPORTED;
}
static int get_param(void* state, const char* id, OrpheusValue* v) {
    DelayState* s = (DelayState*)state;
    if (strcmp(id, "mix") == 0) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->mix; return ORPHEUS_OK; }
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL
};
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void) { return &iface; }
""")


def create_signal_gen():
    comp_dir = BASE / "signal_gen"
    write(comp_dir / "component.yaml", """id: orpheus.builtin.signal_gen
version: 1.0.0
abi_version: 1
package_type: source
sources:
  - src/signal_gen.c
headers:
  - include/orpheus_signal_gen.h
ports:
  - id: out
    direction: output
    type: audio
    sample_format: f32
    channels: param:channels
parameters:
  - id: frequency
    name: Frequency
    type: float
    default: 440.0
    range: [1.0, 20000.0]
    unit: Hz
    update_policy: restart_required
  - id: amplitude
    name: Amplitude
    type: float
    default: 0.5
    range: [0.0, 1.0]
    update_policy: smoothed
  - id: channels
    name: Channels
    type: int
    default: 2
    range: [1, 32]
    update_policy: restart_required
    affects_signature: true
memory:
  state_size: 0
  scratch_size: 0
  alignment: 8
execution:
  sample_rate_independent: false
  latency_samples: 0
  supports_inplace: false
  realtime_safe: true
""")
    write(comp_dir / "include" / "orpheus_signal_gen.h", HEADER_TEMPLATE.format(NAME_UPPER="SIGNAL_GEN"))
    write(comp_dir / "CMakeLists.txt", CMAKE_TEMPLATE.format(name="signal_gen"))
    write(comp_dir / "src" / "signal_gen.c", """#include "orpheus_signal_gen.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float phase;
    float frequency;
    float amplitude;
    uint32_t channels;
} SignalGenState;

static const OrpheusParameter params[] = {
    { .id = "frequency", .name = "Frequency", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 440.0f },
      .min_f32 = 1.0f, .max_f32 = 20000.0f, .unit = "Hz",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .readback = true, .persistent = true },
    { .id = "amplitude", .name = "Amplitude", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.5f },
      .min_f32 = 0.0f, .max_f32 = 1.0f,
      .update_policy = ORPHEUS_UPDATE_SMOOTHED, .readback = true, .persistent = true },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort ports[] = {
    { "out", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.signal_gen", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 1, params, 3, sizeof(SignalGenState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(SignalGenState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { free(state); return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    SignalGenState* s = (SignalGenState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->frequency = 440.0f; s->amplitude = 0.5f;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (!config->param_ids[i]) continue;
        if (strcmp(config->param_ids[i], "frequency") == 0 && config->param_values[i].type == ORPHEUS_VALUE_FLOAT)
            s->frequency = config->param_values[i].value.f32;
        if (strcmp(config->param_ids[i], "amplitude") == 0 && config->param_values[i].type == ORPHEUS_VALUE_FLOAT)
            s->amplitude = config->param_values[i].value.f32;
    }
    s->phase = 0.0f;
    return ORPHEUS_OK;
}
static int reset(void* state) { SignalGenState* s = (SignalGenState*)state; s->phase = 0.0f; return ORPHEUS_OK; }
static int process(void* state, const OrpheusProcessContext* ctx) {
    SignalGenState* s = (SignalGenState*)state;
    OrpheusBuffer* out = ctx->outputs[0];
    if (!out) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    float* out_data = (float*)out->data;
    float step = 2.0f * 3.14159265358979f * s->frequency / ctx->sample_rate;
    for (uint32_t n = 0; n < frames; ++n) {
        float sample = s->amplitude * sinf(s->phase);
        s->phase += step;
        if (s->phase > 2.0f * 3.14159265358979f) s->phase -= 2.0f * 3.14159265358979f;
        for (uint32_t c = 0; c < ch; ++c) out_data[n * ch + c] = sample;
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int set_param(void* state, const char* id, const OrpheusValue* v) {
    SignalGenState* s = (SignalGenState*)state;
    if (strcmp(id, "amplitude") == 0 && v->type == ORPHEUS_VALUE_FLOAT) { s->amplitude = v->value.f32; return ORPHEUS_OK; }
    return ORPHEUS_ERR_UNSUPPORTED;
}
static int get_param(void* state, const char* id, OrpheusValue* v) {
    SignalGenState* s = (SignalGenState*)state;
    if (strcmp(id, "amplitude") == 0) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->amplitude; return ORPHEUS_OK; }
    if (strcmp(id, "frequency") == 0) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->frequency; return ORPHEUS_OK; }
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL
};
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void) { return &iface; }
""")


def create_probe_peak():
    comp_dir = BASE / "probe_peak"
    write(comp_dir / "component.yaml", """id: orpheus.builtin.probe_peak
version: 1.0.0
abi_version: 1
package_type: source
sources:
  - src/probe_peak.c
headers:
  - include/orpheus_probe_peak.h
ports:
  - id: in
    direction: input
    type: audio
    sample_format: f32
    channels: param:channels
  - id: out
    direction: output
    type: audio
    sample_format: f32
    channels: param:channels
parameters:
  - id: channels
    name: Channels
    type: int
    default: 2
    range: [1, 32]
    update_policy: restart_required
    affects_signature: true
  - id: peak
    name: Peak
    type: float
    default: 0.0
    readback: true
    update_policy: immediate
memory:
  state_size: 0
  scratch_size: 0
  alignment: 8
execution:
  sample_rate_independent: true
  latency_samples: 0
  supports_inplace: true
  realtime_safe: true
""")
    write(comp_dir / "include" / "orpheus_probe_peak.h", HEADER_TEMPLATE.format(NAME_UPPER="PROBE_PEAK"))
    write(comp_dir / "CMakeLists.txt", CMAKE_TEMPLATE.format(name="probe_peak"))
    write(comp_dir / "src" / "probe_peak.c", """#include "orpheus_probe_peak.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float peak; uint32_t channels; } ProbePeakState;

static const OrpheusParameter params[] = {
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "peak", .name = "Peak", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .update_policy = ORPHEUS_UPDATE_IMMEDIATE, .readback = true, .persistent = false }
};

static const OrpheusPort ports[] = {
    { "in", ORPHEUS_PORT_INPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "out", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.probe_peak", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 2, params, 2, sizeof(ProbePeakState), 0, 8, 0, true, true
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(ProbePeakState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { free(state); return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    ProbePeakState* s = (ProbePeakState*)state; s->channels = config->channels > 0 ? config->channels : 2; s->peak = 0.0f; return ORPHEUS_OK;
}
static int reset(void* state) { ProbePeakState* s = (ProbePeakState*)state; s->peak = 0.0f; return ORPHEUS_OK; }
static int process(void* state, const OrpheusProcessContext* ctx) {
    ProbePeakState* s = (ProbePeakState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!in || !out) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n = ctx->frame_count * s->channels;
    const float* src = (const float*)in->data;
    float* dst = (float*)out->data;
    float peak = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        dst[i] = src[i];
        float a = fabsf(src[i]);
        if (a > peak) peak = a;
    }
    s->peak = peak;
    out->frame_count = ctx->frame_count;
    return ORPHEUS_OK;
}
static int set_param(void* state, const char* id, const OrpheusValue* v) { (void)state; (void)id; (void)v; return ORPHEUS_ERR_UNSUPPORTED; }
static int get_param(void* state, const char* id, OrpheusValue* v) {
    ProbePeakState* s = (ProbePeakState*)state;
    if (strcmp(id, "peak") == 0) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->peak; return ORPHEUS_OK; }
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL
};
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void) { return &iface; }
""")


def create_probe_waveform():
    comp_dir = BASE / "probe_waveform"
    write(comp_dir / "component.yaml", """id: orpheus.builtin.probe_waveform
version: 1.0.0
abi_version: 1
package_type: source
sources:
  - src/probe_waveform.c
headers:
  - include/orpheus_probe_waveform.h
ports:
  - id: in
    direction: input
    type: audio
    sample_format: f32
    channels: param:channels
  - id: out
    direction: output
    type: audio
    sample_format: f32
    channels: param:channels
parameters:
  - id: channels
    name: Channels
    type: int
    default: 2
    range: [1, 32]
    update_policy: restart_required
    affects_signature: true
memory:
  state_size: 0
  scratch_size: 0
  alignment: 8
execution:
  sample_rate_independent: true
  latency_samples: 0
  supports_inplace: true
  realtime_safe: true
""")
    write(comp_dir / "include" / "orpheus_probe_waveform.h", HEADER_TEMPLATE.format(NAME_UPPER="PROBE_WAVEFORM"))
    write(comp_dir / "CMakeLists.txt", CMAKE_TEMPLATE.format(name="probe_waveform"))
    write(comp_dir / "src" / "probe_waveform.c", """#include "orpheus_probe_waveform.h"

#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t channels; } ProbeWaveformState;

static const OrpheusParameter params[] = {
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort ports[] = {
    { "in", ORPHEUS_PORT_INPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "out", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.probe_waveform", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 2, params, 1, sizeof(ProbeWaveformState), 0, 8, 0, true, true
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(ProbeWaveformState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { free(state); return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    ProbeWaveformState* s = (ProbeWaveformState*)state; s->channels = config->channels > 0 ? config->channels : 2; return ORPHEUS_OK;
}
static int reset(void* state) { (void)state; return ORPHEUS_OK; }
static int process(void* state, const OrpheusProcessContext* ctx) {
    ProbeWaveformState* s = (ProbeWaveformState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!in || !out) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n = ctx->frame_count * s->channels;
    memcpy(out->data, in->data, n * sizeof(float));
    out->frame_count = ctx->frame_count;
    return ORPHEUS_OK;
}
static int set_param(void* state, const char* id, const OrpheusValue* v) { (void)state; (void)id; (void)v; return ORPHEUS_ERR_UNSUPPORTED; }
static int get_param(void* state, const char* id, OrpheusValue* v) {
    ProbeWaveformState* s = (ProbeWaveformState*)state;
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL
};
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void) { return &iface; }
""")


if __name__ == "__main__":
    create_split()
    create_merge()
    create_delay()
    create_signal_gen()
    create_probe_peak()
    create_probe_waveform()
