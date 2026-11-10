"""ABI v7 组件壳生成；用户算法只初始化一次，生成区整目录事务替换。"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import uuid
from pathlib import Path

from .component_definition import (
    ComponentDefinitionError,
    load_component_definition,
    normalize_component_definition,
)

TOOL_ID = "orpheus.component_codegen.v1"
MANIFEST = "component_codegen.json"
_TYPES = {"float": ("float", "FLOAT", "f32"), "int": ("int32_t", "INT", "i32"),
          "bool": ("bool", "BOOL", "b")}


def _error(message, code="generation_error"):
    raise ComponentDefinitionError(message, "generated", code)


def _hash(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _read_configuration(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except OSError as exc:
        raise ComponentDefinitionError(f"无法读取组件配置：{exc}", str(path), "read_error") from exc


def _quote(value) -> str:
    return json.dumps(str(value), ensure_ascii=False)


def _number(value, kind):
    if kind == "bool":
        return "true" if value else "false"
    if kind == "int":
        return "(-2147483647 - 1)" if value == -2147483648 else str(value)
    return f"{float(value):.9e}f"


def _check_features(definition):
    for port in definition["ports"]:
        if port["type"] != "audio" or port["sample_format"] != "f32":
            _error(f"首版仅生成 audio f32 端口：{port['id']}", "unsupported_feature")
        if "count" in port:
            _error(f"首版不生成 count 可变引脚：{port['id']}", "unsupported_feature")
    for param in definition["parameters"]:
        if param["type"] not in _TYPES or param.get("count", 1) != 1:
            _error(f"首版仅生成 float/int/bool 标量参数：{param['id']}", "unsupported_feature")
        if param["update_policy"] not in {"immediate", "block_boundary", "restart_required"}:
            _error(f"首版不生成需要额外状态协议的更新策略：{param['id']}", "unsupported_feature")
    # 大小写不同的端口会产生相同枚举常量。
    names = [p["id"].upper() for p in definition["ports"]]
    if len(set(names)) != len(names):
        _error("端口名称大写后冲突", "symbol_collision")


def _render(definition):
    symbols = definition["algorithm_symbols"]
    prefix, state, io, private, function = (
        symbols[k] for k in ("prefix", "state_type", "io_type", "private_type", "function"))
    upper = prefix.upper()
    ports, params = definition["ports"], definition["parameters"]
    inputs = [p for p in ports if p["direction"] == "input"]
    outputs = [p for p in ports if p["direction"] == "output"]
    api = [
        "#pragma once", '#include "orpheus_abi.h"', "",
        f"typedef struct {prefix}_InputF32 {{",
        "    const OrpheusBuffer* buffer;", "    const float* samples;",
        f"}} {prefix}_InputF32;",
        f"typedef struct {prefix}_OutputF32 {{",
        "    OrpheusBuffer* buffer;", "    float* samples;", "    OrpheusDelivery delivery;",
        f"}} {prefix}_OutputF32;", "",
    ]
    if inputs:
        api.append("enum {")
        api.extend(f"    {upper}_INPUT_{p['id'].upper()} = {i}," for i, p in enumerate(inputs))
        api.append("};")
    driven = [p for p in outputs if "drive_port_no" in p]
    # UINT32_MAX-1 超过 C11 enum 的 int 范围，使用强类型常量宏。
    api.extend(f"#define {upper}_DRIVE_{p['id'].upper()} UINT32_C({p['drive_port_no']})" for p in driven)
    api += [f"typedef struct {io} {{"]
    api.extend(f"    {prefix}_{'Input' if p['direction'] == 'input' else 'Output'}F32 {p['id']};" for p in ports)
    if not ports:
        api.append("    unsigned char reserved;")
    api += [f"}} {io};", ""]
    for port in driven:
        name = port["id"]
        api += [
            f"static inline int {prefix}_emit_{name}(const OrpheusProcessContext* ctx,",
            f"        const {prefix}_OutputF32* output) {{",
            "    if (!output || output->delivery.buffer != output->buffer)",
            "        return ORPHEUS_ERR_INVALID_ARG;",
            f"    return orpheus_emit(ctx, {upper}_DRIVE_{name.upper()}, &output->delivery);", "}",
        ]
    state_header = [
        "#pragma once", '#include "component_api.h"', '#include "../user/algorithm.h"', "",
        f"typedef struct {state} {{",
    ]
    state_header.extend(f"    {_TYPES[p['type']][0]} {p['id']};" for p in params)
    state_header += [f"    {private} algorithm;", f"}} {state};", "",
                     f"int {function}({state}* state, {io}* io, const OrpheusProcessContext* ctx);"]
    user_header = [
        "#pragma once", "", "/* 用户私有状态可在此扩展；实时处理不分配内存。 */",
        f"typedef struct {private} {{", "    unsigned char reserved;", f"}} {private};",
    ]
    user_source = [
        '#include "component_state.h"', '#include "component_api.h"', "",
        f"int {function}({state}* state, {io}* io, const OrpheusProcessContext* ctx) {{",
        "    (void)state; (void)io; (void)ctx;",
        "    /* 请实现算法，并在数据就绪后显式调用对应 emit 函数。 */",
        "    return ORPHEUS_ERR_UNSUPPORTED;", "}",
    ]
    shell = [
        '#include "component_state.h"', "#include <string.h>", "#include <math.h>", "",
    ]
    by_id = {p["id"]: p for p in ports}

    def expression(port, field, context=False):
        value = port.get(field, 0)
        if isinstance(value, int):
            return str(value)
        target, attr = value.split(":")
        if target == "param":
            return f"s->{attr}" if context else "0"
        return expression(by_id[target], attr, context)

    if ports:
        shell.append("static const OrpheusPort ports[] = {")
        for port in ports:
            variable = isinstance(port["channels"], str)
            channel_param = port["channels"].split(":")[1] if variable and port["channels"].startswith("param:") else None
            shell.append(
                f"    {{.id={_quote(port['id'])}, .direction=ORPHEUS_PORT_{port['direction'].upper()}, "
                f".type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, "
                f".channels={expression(port, 'channels')}, .sample_rate={expression(port, 'sample_rate')}, "
                f".block_size={expression(port, 'block_size')}, .is_variable={'true' if variable else 'false'}, "
                f".channels_param={_quote(channel_param) if channel_param else 'NULL'}}},")
        shell += ["};", ""]
    if params:
        shell.append("static const OrpheusParameter params[] = {")
        for param in params:
            _, enum, field = _TYPES[param["type"]]
            bounds = param.get("range")
            bounds_code = ""
            if bounds:
                suffix = "i32" if param["type"] == "int" else "f32"
                bounds_code = f", .min_{suffix}={_number(bounds[0], param['type'])}, .max_{suffix}={_number(bounds[1], param['type'])}"
            shell.append(
                f"    {{.id={_quote(param['id'])}, .name={_quote(param.get('name', param['id']))}, "
                f".type=ORPHEUS_VALUE_{enum}, .default_value={{.type=ORPHEUS_VALUE_{enum}, "
                f".value.{field}={_number(param['default'], param['type'])}}}, "
                f".update_policy=ORPHEUS_UPDATE_{param['update_policy'].upper()}, "
                f".unit={_quote(param.get('unit', ''))}, "
                f".readback={str(param.get('readback', True)).lower()}, "
                f".persistent={str(param.get('persistent', True)).lower()}, "
                f".affects_signature={str(param.get('affects_signature', False)).lower()}{bounds_code}}},")
        shell += ["};", ""]
    shell += [
        "static const OrpheusComponentDescriptor descriptor = {",
        f"    .id={_quote(definition['id'])}, .version={_quote(definition['version'])}, .abi_version=7,",
        f"    .ports={'ports' if ports else 'NULL'}, .port_count={len(ports)},",
        f"    .params={'params' if params else 'NULL'}, .param_count={len(params)},",
        f"    .state_size=sizeof({state}), .alignment=_Alignof({state}), .realtime_safe=true",
        "};",
        "static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }", "",
        "static int set_parameter(void* state, const char* key, const OrpheusValue* value) {",
        f"    {state}* s = ({state}*)state;",
        "    if (!s || !key || !value) return ORPHEUS_ERR_INVALID_ARG;",
    ]
    for param in params:
        _, enum, field = _TYPES[param["type"]]
        shell += [f"    if (strcmp(key, {_quote(param['id'])}) == 0) {{",
                  f"        if (value->type != ORPHEUS_VALUE_{enum}) return ORPHEUS_ERR_INVALID_ARG;"]
        if param["type"] == "float":
            shell.append(f"        if (!isfinite(value->value.{field})) return ORPHEUS_ERR_INVALID_ARG;")
        if "range" in param:
            lower, high = (_number(v, param["type"]) for v in param["range"])
            shell.append(f"        if (value->value.{field} < {lower} || value->value.{field} > {high}) return ORPHEUS_ERR_INVALID_ARG;")
        if param.get("affects_signature") and "range" not in param:
            shell.append(f"        if (value->value.{field} < 1) return ORPHEUS_ERR_INVALID_ARG;")
        shell += [f"        s->{param['id']} = value->value.{field}; return ORPHEUS_OK;", "    }"]
    shell += ["    return ORPHEUS_ERR_NOT_FOUND;", "}",
              "static int get_parameter(void* state, const char* key, OrpheusValue* value) {",
              f"    {state}* s = ({state}*)state;",
              "    if (!s || !key || !value) return ORPHEUS_ERR_INVALID_ARG;"]
    for param in params:
        _, enum, field = _TYPES[param["type"]]
        shell += [f"    if (strcmp(key, {_quote(param['id'])}) == 0) {{",
                  f"        value->type=ORPHEUS_VALUE_{enum}; value->value.{field}=s->{param['id']}; return ORPHEUS_OK;", "    }"]
    shell += [
        "    return ORPHEUS_ERR_NOT_FOUND;", "}",
        "static int prepare(void* state, const OrpheusConfig* config) {",
        f"    {state}* s = ({state}*)state;",
        "    if (!s || !config || (config->param_count && (!config->param_ids || !config->param_values)))",
        "        return ORPHEUS_ERR_INVALID_ARG;",
        f"    {state} candidate = *s;",
        "    for (uint32_t i=0; i<config->param_count; ++i) {",
        "        int result=set_parameter(&candidate, config->param_ids[i], &config->param_values[i]);",
        "        if (result != ORPHEUS_OK) return result;", "    }",
        "    *s=candidate; return ORPHEUS_OK;", "}",
        "static int create(void** state, const OrpheusConfig* config) {",
        "    if (!state || !config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;",
        "    *state=NULL;",
        f"    if ((uintptr_t)config->state_block % _Alignof({state})) return ORPHEUS_ERR_INVALID_ARG;",
        f"    {state}* s=({state}*)config->state_block;", "    memset(s, 0, sizeof(*s));",
    ]
    shell.extend(f"    s->{p['id']}={_number(p['default'], p['type'])};" for p in params)
    shell += [
        "    int result=prepare(s, config);",
        "    if (result == ORPHEUS_OK) *state=s;", "    return result;", "}",
        "static int destroy(void* state) { return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG; }",
        "static int reset(void* state) {",
        f"    {state}* s=({state}*)state;", "    if (!s) return ORPHEUS_ERR_INVALID_ARG;",
        "    memset(&s->algorithm, 0, sizeof(s->algorithm)); return ORPHEUS_OK;", "}",
        "static int register_slots(void* state, const OrpheusRegistry* reg) {",
        f"    {state}* s=({state}*)state;",
        "    if (!s || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;",
    ]
    for param in params:
        _, enum, _ = _TYPES[param["type"]]
        flags = []
        if param.get("readback", True):
            flags.append("ORPHEUS_SLOT_READBACK")
        if param.get("persistent", True):
            flags.append("ORPHEUS_SLOT_PERSISTENT")
        if param.get("affects_signature"):
            flags.append("ORPHEUS_SLOT_AFFECTS_SIGNATURE")
        # 通过回调校验范围，不允许 Runtime 绕过参数校验直接写入。
        extra = ""
        if "range" in param:
            suffix = "i32" if param["type"] == "int" else "f32"
            extra = "".join(f", .{label}_{suffix}={_number(value, param['type'])}"
                            for label, value in zip(("min", "max"), param["range"]))
        shell += [
            f"    if (ORPHEUS_REG_SLOT(reg, s, {param['id']}, ORPHEUS_SLOT_SETTING, "
            f"{_quote(param['id'])}, {_quote(param.get('name', param['id']))}, ORPHEUS_VALUE_{enum}, "
            f".update_policy=ORPHEUS_UPDATE_{param['update_policy'].upper()}, "
            f".flags={' | '.join(flags) or '0'}{extra}) == ORPHEUS_SLOT_ID_INVALID)",
            "        return ORPHEUS_ERR_PROCESSING;",
        ]
    shell += ["    return ORPHEUS_OK;", "}",
              "static int valid_buffer(const OrpheusBuffer* buffer, uint32_t channels) {",
              "    return !buffer || (buffer->format == ORPHEUS_FORMAT_F32 && buffer->interleaved",
              "        && buffer->channels == channels && channels > 0",
              "        && buffer->frame_count <= buffer->frame_capacity",
              "        && (!buffer->frame_capacity || buffer->data)",
              "        && (!buffer->data || (uintptr_t)buffer->data % _Alignof(float) == 0)",
              "        && (size_t)buffer->frame_capacity <= SIZE_MAX / sizeof(float) / channels);", "}",
              "static int process(void* state, const OrpheusProcessContext* ctx) {",
              f"    {state}* s=({state}*)state;", f"    {io} io={{0}};",
              "    if (!s || !ctx) return ORPHEUS_ERR_INVALID_ARG;"]
    for group, direction in ((inputs, "input"), (outputs, "output")):
        for index, port in enumerate(group):
            name = port["id"]
            shell += [
                f"    io.{name}.buffer=(ctx->{direction}s && ctx->{direction}_count > {index}) ? ctx->{direction}s[{index}] : NULL;",
                f"    if (!valid_buffer(io.{name}.buffer, (uint32_t)({expression(port, 'channels', True)})))",
                "        return ORPHEUS_ERR_INVALID_ARG;",
                f"    io.{name}.samples=io.{name}.buffer ? ({'const ' if direction == 'input' else ''}float*)io.{name}.buffer->data : NULL;",
            ]
            if direction == "output":
                shell += [
                    f"    if (io.{name}.buffer) {{",
                    f"        if (ctx->frame_count > io.{name}.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;",
                    f"        io.{name}.buffer->frame_count=ctx->frame_count;", "    }",
                    f"    io.{name}.delivery=(OrpheusDelivery){{.buffer=io.{name}.buffer, .frame_index=ctx->frame_index,",
                    "        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};",
                ]
    shell += [f"    return {function}(s, &io, ctx);", "}",
              "static const OrpheusComponentInterface interface = {",
              "    .get_descriptor=get_descriptor, .create=create, .destroy=destroy,",
              "    .prepare=prepare, .reset=reset, .process=process,",
              "    .set_parameter=set_parameter, .get_parameter=get_parameter, .register_slots=register_slots",
              "};", "#ifndef ORPHEUS_ENTRY_NAME", "#define ORPHEUS_ENTRY_NAME orpheus_get_interface",
              "#endif", "ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }"]
    if not ports:
        start = shell.index("static int valid_buffer(const OrpheusBuffer* buffer, uint32_t channels) {")
        end = shell.index("static int process(void* state, const OrpheusProcessContext* ctx) {")
        del shell[start:end]
    cmake_sources = [
        'set(ORPHEUS_COMPONENT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/component_shell.c"',
        '    "${CMAKE_CURRENT_LIST_DIR}/../user/algorithm.c")',
        'set(ORPHEUS_COMPONENT_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/.."',
        '    "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../user")',
    ]
    cmake = [
        "cmake_minimum_required(VERSION 3.16)", f"project({prefix} C)",
        "set(CMAKE_C_STANDARD 11)", "set(CMAKE_C_STANDARD_REQUIRED ON)",
        'include("${CMAKE_CURRENT_LIST_DIR}/generated/component_sources.cmake")',
        f"add_library({prefix} SHARED ${{ORPHEUS_COMPONENT_SOURCES}})",
        f"target_include_directories({prefix} PUBLIC ${{ORPHEUS_COMPONENT_INCLUDE_DIRS}})",
        "if(TARGET orpheus_abi)",
        f"    target_link_libraries({prefix} PUBLIC orpheus_abi)",
        "else()",
        '    set(ORPHEUS_ABI_INCLUDE_DIR "" CACHE PATH "orpheus_abi.h 所在目录")',
        '    if(NOT EXISTS "${ORPHEUS_ABI_INCLUDE_DIR}/orpheus_abi.h")',
        '        message(FATAL_ERROR "请设置 ORPHEUS_ABI_INCLUDE_DIR")',
        "    endif()",
        f'    target_include_directories({prefix} PUBLIC "${{ORPHEUS_ABI_INCLUDE_DIR}}")',
        "endif()",
        "# 空生成器表达式避免多配置构建器自动追加 Debug/Release 子目录。",
        f"set_target_properties({prefix} PROPERTIES",
        '    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components$<0:>"',
        '    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components$<0:>"',
        '    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components$<0:>")',
        f"target_compile_definitions({prefix} PRIVATE ORPHEUS_BUILDING_COMPONENT)",
        "if(MSVC)", f"    target_compile_options({prefix} PRIVATE /utf-8)", "endif()",
    ]
    generated = dict(zip(("component_api.h", "component_state.h", "component_shell.c", "component_sources.cmake"),
                         ("\n".join(lines).rstrip("\n") + "\n" for lines in (api, state_header, shell, cmake_sources))))
    user = {"user/algorithm.h": "\n".join(user_header).rstrip("\n") + "\n",
            "user/algorithm.c": "\n".join(user_source).rstrip("\n") + "\n",
            "CMakeLists.txt": "\n".join(cmake).rstrip("\n") + "\n"}
    return generated, user


def _owned_manifest(directory: Path):
    if directory.is_symlink() or (directory.exists() and not directory.is_dir()):
        _error("生成区不能是文件或符号链接", "ownership_error")
    if not directory.exists():
        return None
    try:
        manifest_path = directory / MANIFEST
        if manifest_path.is_symlink():
            _error("生成区清单不能是符号链接", "ownership_error")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest["tool"] != TOOL_ID or not isinstance(manifest["files"], dict):
            raise ValueError("tool")
        expected = {"component_api.h", "component_state.h", "component_shell.c", "component_sources.cmake"}
        if set(manifest["files"]) != expected:
            raise ValueError("files")
        if {p.name for p in directory.iterdir()} != expected | {MANIFEST}:
            _error("生成区存在不属于本工具的文件，拒绝替换", "ownership_error")
        for name, digest in manifest["files"].items():
            path = directory / name
            if path.is_symlink() or not path.is_file() or _hash(path.read_bytes()) != digest:
                _error(f"生成区文件被修改或不是普通文件：{name}", "ownership_error")
        return manifest
    except (OSError, ValueError, KeyError, TypeError) as exc:
        _error(f"生成区缺少有效的所有权清单：{exc}", "ownership_error")


def assert_component_generated_current(path: Path) -> None:
    """构建前调用：校验配置、生成区所有权和配置字节摘要；过期抛出结构化异常。"""
    path = Path(path).resolve()
    original = _read_configuration(path)
    definition = normalize_component_definition(load_component_definition(path))
    _check_features(definition)
    manifest = _owned_manifest(path.parent / "generated")
    if _read_configuration(path) != original:
        _error("配置在检查期间发生变化，请重试", "configuration_changed")
    if manifest is None or manifest.get("configuration_sha256") != _hash(original):
        _error("组件配置已变化或尚未生成，请重新执行 generate_component", "stale_generated")
    generated, _ = _render(definition)
    if manifest["files"] != {name: _hash(content.encode("utf-8")) for name, content in generated.items()}:
        _error("生成器输出已变化，请重新生成组件", "stale_generated")


def check_generated(path: Path) -> None:
    """构建接入的简短公共名称；失败诊断与 assert_component_generated_current 相同。"""
    assert_component_generated_current(path)


def generate_component(path: Path) -> list[Path]:
    """仅由落盘 YAML 生成；成功返回生成文件及首次创建的用户文件（绝对路径）。"""
    path = Path(path).resolve()
    original = _read_configuration(path)
    definition = normalize_component_definition(load_component_definition(path))
    if _read_configuration(path) != original:
        _error("配置在加载期间发生变化，请重试", "configuration_changed")
    _check_features(definition)
    generated, user = _render(definition)
    root = path.parent
    destination = root / "generated"
    _owned_manifest(destination)
    for relative in user:
        target = root / relative
        if target.is_symlink() or target.parent.is_symlink() or (target.exists() and not target.is_file()):
            _error(f"用户文件路径不是普通文件：{relative}", "ownership_error")
    manifest = {"tool": TOOL_ID, "configuration_sha256": _hash(original),
                "files": {name: _hash(content.encode("utf-8")) for name, content in generated.items()}}
    generated[MANIFEST] = json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2) + "\n"
    lock = root / ".component_codegen.lock"
    try:
        lock_fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        _error("已有组件生成操作进行中（或遗留锁），拒绝并发写入", "generation_locked")
    stage = root / f".component_codegen_stage_{uuid.uuid4().hex}"
    backup = root / f".component_codegen_backup_{uuid.uuid4().hex}"
    created = []
    swapped = False
    user_directory_created = False
    try:
        os.close(lock_fd)
        _owned_manifest(destination)
        stage.mkdir()
        for name, content in generated.items():
            (stage / name).write_bytes(content.encode("utf-8"))
        if not (root / "user").exists():
            (root / "user").mkdir()
            user_directory_created = True
        for relative, content in user.items():
            target = root / relative
            if not target.exists():
                with target.open("xb") as stream:
                    created.append(target)
                    stream.write(content.encode("utf-8"))
        if _read_configuration(path) != original:
            _error("配置在生成期间发生变化，请重试", "configuration_changed")
        _owned_manifest(destination)
        if destination.exists():
            destination.rename(backup)
        try:
            stage.rename(destination)
            swapped = True
        except BaseException:
            if backup.exists():
                backup.rename(destination)
            raise
        result = [destination / name for name in generated] + created
    except BaseException:
        for target in reversed(created):
            target.unlink()
        if user_directory_created:
            (root / "user").rmdir()
        raise
    finally:
        if stage.exists():
            shutil.rmtree(stage)
        lock.unlink(missing_ok=True)
    if swapped and backup.exists():
        shutil.rmtree(backup)
    return result
