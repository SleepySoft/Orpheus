"""自定义组件壳脚手架：生成 ABI 骨架 + 用户文件（隔离，重生成不覆盖）。

生成物：
- component.yaml        manifest（端口/参数/custom_handles/state_type）
- include/orpheus_<n>.h 公开状态结构体（骨架）
- src/<n>.c             ABI 样板：descriptor/create/prepare/process/register_slots/hook
- user/<n>_user.c/.h    用户代码（生成器永不触碰）
- CMakeLists.txt        把 src/ 与 user/ 一起编译
"""

from __future__ import annotations

from pathlib import Path


def _camel(name: str) -> str:
    return "".join(p[:1].upper() + p[1:] for p in name.split("_") if p)


def scaffold_custom_component(root: Path, name: str, category: str = "自定义") -> Path:
    """在 components/orpheus/builtin/<name>/ 生成自定义组件壳。"""
    if not name.isidentifier():
        raise ValueError(f"组件名必须是合法标识符: {name}")
    comp_dir = root / "components" / "orpheus" / "builtin" / name
    if (comp_dir / "component.yaml").exists():
        raise FileExistsError(f"组件已存在: {comp_dir}")
    for sub in ("src", "include", "user"):
        (comp_dir / sub).mkdir(parents=True, exist_ok=True)
    camel = _camel(name)
    upper = name.upper()

    yaml = f"""id: orpheus.builtin.{name}
name: {camel}
category: {category}
version: 1.0.0
abi_version: 1
package_type: source
state_type: {camel}State
sources:
  - src/{name}.c
  - user/{name}_user.c
headers:
  - include/orpheus_{name}.h
  - user/{name}_user.h
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
    name: 通道数
    type: int
    default: 2
    range: [1, 32]
    update_policy: restart_required
    affects_signature: true
  - id: mix
    name: 混合比
    type: float
    default: 0.5
    range: [0.0, 1.0]
    update_policy: smoothed
custom_handles:
  - id: reset
    name: 重置
    reply: false
  - id: snapshot
    name: 状态快照
    reply: true
memory:
  state_size: 0
  scratch_size: 0
  alignment: 8
execution:
  sample_rate_independent: true
  latency_samples: 0
  supports_inplace: true
  realtime_safe: true
"""

    header = f"""#ifndef ORPHEUS_{upper}_H
#define ORPHEUS_{upper}_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {{
#endif

/* {camel}State：公开状态结构体（骨架字段；算法参数可在此扩展，user 侧读写）。 */
typedef struct {{
    uint32_t channels;
    float mix;   /* 混合比（示例参数） */
}} {camel}State;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}}
#endif

#endif /* ORPHEUS_{upper}_H */
"""

    src = f"""#include "orpheus_{name}.h"
#include "{name}_user.h"

#include <stdlib.h>
#include <string.h>

static const OrpheusParameter {name}_params[] = {{
    {{ .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = {{ .type = ORPHEUS_VALUE_INT, .value.i32 = 2 }},
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .persistent = true, .readback = true, .affects_signature = true }},
    {{ .id = "mix", .name = "混合比", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = {{ .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.5f }},
      .min_f32 = 0.0f, .max_f32 = 1.0f,
      .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .persistent = true }}
}};

static const OrpheusPort {name}_ports[] = {{
    {{ .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }},
    {{ .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }}
}};

static const OrpheusComponentDescriptor {name}_descriptor = {{
    .id = "orpheus.builtin.{name}", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = {name}_ports, .port_count = 2,
    .params = {name}_params, .param_count = 2,
    .state_size = sizeof({camel}State), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
}};

static const OrpheusComponentDescriptor* {name}_get_descriptor(void) {{
    return &{name}_descriptor;
}}

static int {name}_create(void** state, const OrpheusConfig* config) {{
    if (config != NULL && config->state_block != NULL) {{
        *state = config->state_block;
        return ORPHEUS_OK;
    }}
    *state = calloc(1, sizeof({camel}State));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}}

static int {name}_destroy(void* state) {{
    (void)state;  /* v2：状态块由 Runtime 统一管理 */
    return ORPHEUS_OK;
}}

static int {name}_prepare(void* state, const OrpheusConfig* config) {{
    {camel}State* s = ({camel}State*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->mix = 0.5f;
    for (uint32_t i = 0; i < config->param_count; ++i) {{
        if (!config->param_ids[i]) continue;
        if (strcmp(config->param_ids[i], "mix") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_FLOAT) {{
            s->mix = config->param_values[i].value.f32;
        }}
    }}
    return {name}_user_prepare(s, config);
}}

static int {name}_reset(void* state) {{
    return {name}_user_reset(({camel}State*)state);
}}

static int {name}_process(void* state, const OrpheusProcessContext* ctx) {{
    return {name}_user_process(({camel}State*)state, ctx);
}}

static int {name}_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {{
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}}

static int {name}_get_parameter(void* state, const char* param_id, OrpheusValue* value) {{
    {camel}State* s = ({camel}State*)state;
    if (strcmp(param_id, "channels") == 0) {{
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }}
    if (strcmp(param_id, "mix") == 0) {{
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->mix;
        return ORPHEUS_OK;
    }}
    return ORPHEUS_ERR_NOT_FOUND;
}}

static int {name}_register_slots(void* state, const OrpheusRegistry* reg) {{
    {camel}State* s = ({camel}State*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32 = 1, .max_i32 = 32,
                     .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags = ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                              ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, mix, ORPHEUS_SLOT_SETTING, "mix", "混合比",
                     ORPHEUS_VALUE_FLOAT, .min_f32 = 0.0f, .max_f32 = 1.0f,
                     .update_policy = ORPHEUS_UPDATE_SMOOTHED,
                     .flags = ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DIRECT_WRITE);
    return ORPHEUS_OK;
}}

/* 统一 hook：外部注册优先；组件 hook 把消息转给 user 实现（CUSTOM 入口）。 */
static int {name}_hook(void* state, uint32_t id, uint32_t event,
                       const OrpheusBlob* req, OrpheusBlob* resp) {{
    return {name}_user_handle(({camel}State*)state, id, event, req, resp);
}}

static const OrpheusComponentInterface {name}_interface = {{
    .get_descriptor = {name}_get_descriptor, .create = {name}_create, .destroy = {name}_destroy,
    .prepare = {name}_prepare, .reset = {name}_reset, .process = {name}_process,
    .set_parameter = {name}_set_parameter, .get_parameter = {name}_get_parameter,
    .get_state_value = NULL, .register_slots = {name}_register_slots,
    .hook = {name}_hook
}};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {{
    return &{name}_interface;
}}
"""

    user_h = f"""#ifndef ORPHEUS_{upper}_USER_H
#define ORPHEUS_{upper}_USER_H

#include "orpheus_abi.h"
#include "orpheus_{name}.h"

#ifdef __cplusplus
extern "C" {{
#endif

/* 用户入口点：只改 user/{name}_user.c。此文件由脚手架创建后生成器不再触碰。 */
int {name}_user_prepare({camel}State* s, const OrpheusConfig* config);
int {name}_user_reset({camel}State* s);
int {name}_user_process({camel}State* s, const OrpheusProcessContext* ctx);
int {name}_user_handle({camel}State* s, uint32_t id, uint32_t event,
                       const OrpheusBlob* req, OrpheusBlob* resp);

#ifdef __cplusplus
}}
#endif

#endif /* ORPHEUS_{upper}_USER_H */
"""

    user_c = f"""#include "{name}_user.h"

#include <string.h>

/* ===== 用户代码（生成器永不覆盖此文件）=====
 * - prepare/reset/process：DSP 算法实现；
 * - handle：CUSTOM 消息（req->data/len → resp->data/len；resp==NULL 表示 notification）。
 *   返回 ORPHEUS_HOOK_HANDLED = 已处理；ORPHEUS_HOOK_CONTINUE = 交给默认语义。
 */

int {name}_user_prepare({camel}State* s, const OrpheusConfig* config) {{
    (void)s; (void)config;
    return ORPHEUS_OK;
}}

int {name}_user_reset({camel}State* s) {{
    (void)s;
    return ORPHEUS_OK;
}}

int {name}_user_process({camel}State* s, const OrpheusProcessContext* ctx) {{
    /* 默认直通：输入 → 输出 */
    if (!ctx->inputs[0] || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n = ctx->frame_count * s->channels;
    memcpy(ctx->outputs[0]->data, ctx->inputs[0]->data, n * sizeof(float));
    ctx->outputs[0]->frame_count = ctx->frame_count;
    return ORPHEUS_OK;
}}

int {name}_user_handle({camel}State* s, uint32_t id, uint32_t event,
                       const OrpheusBlob* req, OrpheusBlob* resp) {{
    (void)s; (void)id; (void)event; (void)req; (void)resp;
    return ORPHEUS_HOOK_CONTINUE;
}}
"""

    cmake = f"""cmake_minimum_required(VERSION 3.16)

set(COMPONENT_NAME orpheus_builtin_{name})

add_library(${{COMPONENT_NAME}} SHARED)

target_sources(${{COMPONENT_NAME}}
    PRIVATE
        src/{name}.c
        user/{name}_user.c
)

target_include_directories(${{COMPONENT_NAME}}
    PUBLIC
        ${{CMAKE_CURRENT_SOURCE_DIR}}/include
        ${{CMAKE_CURRENT_SOURCE_DIR}}/user
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

    (comp_dir / "component.yaml").write_text(yaml, encoding="utf-8")
    (comp_dir / "include" / f"orpheus_{name}.h").write_text(header, encoding="utf-8")
    (comp_dir / "src" / f"{name}.c").write_text(src, encoding="utf-8")
    (comp_dir / "user" / f"{name}_user.h").write_text(user_h, encoding="utf-8")
    (comp_dir / "user" / f"{name}_user.c").write_text(user_c, encoding="utf-8")
    (comp_dir / "CMakeLists.txt").write_text(cmake, encoding="utf-8")
    return comp_dir
