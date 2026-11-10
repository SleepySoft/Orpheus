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
    LIFECYCLE_HOOKS,
    encode_array_initializer,
    is_array_parameter,
    load_component_definition,
    normalize_component_definition,
)

TOOL_ID = "orpheus.component_codegen.v1"
MANIFEST = "component_codegen.json"
_TYPES = {"float": ("float", "FLOAT", "f32"), "int": ("int32_t", "INT", "i32"),
          "bool": ("bool", "BOOL", "b"), "string": ("char", "STRING", "str")}
_FORMATS = {"f32": ("float", "F32"), "i32": ("int32_t", "I32"), "i16": ("int16_t", "I16")}


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
    escapes = {"\\": "\\\\", '"': '\\"', "?": "\\?"}
    return '"' + "".join(escapes.get(c, f"\\{ord(c):03o}" if ord(c) < 32 or ord(c) == 127 else c)
                         for c in str(value)) + '"'


def _number(value, kind):
    if kind == "string":
        return _quote(value)
    if kind == "bool":
        return "true" if value else "false"
    if kind == "int":
        return "(-2147483647 - 1)" if value == -2147483648 else str(value)
    return f"{float(value):.9e}f"


def _check_features(definition):
    for port in definition["ports"]:
        if port["type"] == "trigger":
            continue
        if port["sample_format"] not in _FORMATS:
            _error(f"尚不生成压缩 i24 视图：{port['id']}", "unsupported_feature")
    for param in definition["parameters"]:
        if param["type"] not in _TYPES:
            _error(f"不支持的参数元素类型：{param['id']}", "unsupported_feature")
        if param["type"] != "string" and not _is_array(param) and param.get("shape"):
            _error(f"数值数组必须显式声明固定 count：{param['id']}", "unsupported_feature")
    if definition.get("execution", {}).get("none"):
        _error("声明式 execution.none 节点不适用可执行组件壳", "unsupported_feature")
    if definition.get("memory", {}).get("state_size", 0):
        _error("state_size 必须由生成的 State 类型确定，不能另行手写", "unsupported_feature")
    # 大小写不同的端口会产生相同枚举常量。
    names = [p["id"].upper() for p in definition["ports"]]
    if len(set(names)) != len(names):
        _error("端口名称大写后冲突", "symbol_collision")


def _parameter_declaration(param):
    if param["type"] == "string":
        return f"char {param['id']}[{param.get('capacity', 256)}];"
    count = f"[{param['count']}]" if _is_array(param) else ""
    return f"{_TYPES[param['type']][0]} {param['id']}{count};"


def _is_array(param):
    return is_array_parameter(param)


def _csv_default(param):
    return encode_array_initializer(param, param["default"])


def _private_resource_fields(definition):
    tree = {}
    for resource in definition.get("bulk_bindings", []):
        if not resource["storage"].startswith("algorithm."):
            continue
        node = tree
        fields = resource["storage"].split(".")[1:]
        for field in fields[:-1]:
            node = node.setdefault(field, {})
        node[fields[-1]] = resource

    def render(node, indent):
        lines = []
        for name, value in node.items():
            if "storage" in value and isinstance(value["storage"], str):
                lines.append(f"{indent}{_TYPES[value['type']][0]} {name}[{value['count']}];")
            else:
                lines.append(f"{indent}struct {{")
                lines.extend(render(value, indent + "    "))
                lines.append(f"{indent}}} {name};")
        return lines

    return render(tree, "    ") or ["    unsigned char reserved;"]

def _render_resource_counts(definition, values):
    resources = definition.get("bulk_bindings", [])
    lines = []
    for resource in resources:
        key, capacity = resource["id"], resource["count"]
        lines += [f"static int resource_count_{key}(const {values}* p, uint32_t* count) {{",
                  "    (void)p; uint32_t product=1;"]
        for i, dimension in enumerate(resource["shape"]):
            term = f"p->{dimension.split(':')[1]}" if isinstance(dimension, str) else str(dimension)
            lines += [f"    int64_t dimension{i}={term};",
                      f"    if (dimension{i}<1 || (uint64_t)dimension{i}>{capacity}u/product) return ORPHEUS_ERR_INVALID_ARG;",
                      f"    product*=(uint32_t)dimension{i};"]
        lines += ["    *count=product; return ORPHEUS_OK;", "}"]
    if resources:
        lines += [f"static int validate_resource_counts(const {values}* p) {{",
                  "    uint32_t count; int result;"]
        for resource in resources:
            lines += [f"    result=resource_count_{resource['id']}(p, &count);",
                      "    if (result!=ORPHEUS_OK) return result;"]
        lines += ["    return ORPHEUS_OK;", "}"]
    return lines


def _port_suffix(port):
    return "Trigger" if port["type"] == "trigger" else _FORMATS[port["sample_format"]][1]


def _lifecycle_signatures(state):
    return {
        "prepare": f"{state}* state, const OrpheusConfig* config",
        "reset": f"{state}* state", "destroy": f"{state}* state",
        "set_parameter": f"{state}* state, const char* key, const OrpheusValue* value",
        "get_parameter": f"{state}* state, const char* key, OrpheusValue* value",
        "register_slots": f"{state}* state, const OrpheusRegistry* reg",
        "hook": f"{state}* state, uint32_t id, uint32_t event, const OrpheusBlob* req, OrpheusBlob* resp",
    }


def _render_parameter_shell(definition):
    """生成参数事务和生命周期适配；绝不复制含用户资源指针的 AlgorithmState。"""
    symbols = definition["algorithm_symbols"]
    prefix, state = symbols["prefix"], symbols["state_type"]
    params = definition["parameters"]
    hooks = set(definition.get("algorithm", {}).get("lifecycle", []))
    values = f"{prefix}_Parameters"
    lines = [f"typedef struct {values} {{"]
    lines += [f"    {_parameter_declaration(p)}" for p in params] or ["    unsigned char reserved;"]
    lines += [f"}} {values};",
              f"static void snapshot_parameters(const {state}* s, {values}* p) {{",
              "    (void)s; memset(p, 0, sizeof(*p));"]
    for p in params:
        key = p["id"]
        lines.append(f"    memcpy(p->{key}, s->{key}, sizeof(p->{key}));" if p["type"] == "string" or _is_array(p)
                     else f"    p->{key}=s->{key};")
    lines += ["}", f"static void commit_parameters({state}* s, const {values}* p) {{",
              "    (void)s; (void)p;"]
    for p in params:
        key = p["id"]
        lines.append(f"    memcpy(s->{key}, p->{key}, sizeof(s->{key}));" if p["type"] == "string" or _is_array(p)
                     else f"    s->{key}=p->{key};")
    lines += ["}", f"static void default_parameters({values}* p) {{", "    memset(p, 0, sizeof(*p));"]
    for p in params:
        key = p["id"]
        if _is_array(p):
            lines += [f"    const {_TYPES[p['type']][0]} initial_{key}[{p['count']}]={{" +
                      ", ".join(_number(v, p["type"]) for v in p["default"]) + "};",
                      f"    memcpy(p->{key}, initial_{key}, sizeof(p->{key}));"]
        else:
            lines.append(f"    memcpy(p->{key}, {_quote(p['default'])}, {len(p['default'].encode('utf-8')) + 1});"
                         if p["type"] == "string" else f"    p->{key}={_number(p['default'], p['type'])};")
    lines += ["}"]
    lines += _render_resource_counts(definition, values)
    if any(p["type"] == "string" for p in params):
        lines += [
            "static int valid_utf8_string(const char* text, size_t capacity, size_t* length) {",
            "    if (!text) return ORPHEUS_ERR_INVALID_ARG;",
            "    size_t i=0;",
            "    while (i<capacity) {",
            "        unsigned char c=(unsigned char)text[i];",
            "        if (!c) { *length=i; return ORPHEUS_OK; }",
            "        if (c<0x80) { ++i; continue; }",
            "        size_t n=(c>=0xC2 && c<=0xDF) ? 2 : (c>=0xE0 && c<=0xEF) ? 3 :",
            "                 (c>=0xF0 && c<=0xF4) ? 4 : 0;",
            "        if (!n || n>capacity-i) return ORPHEUS_ERR_INVALID_ARG;",
            "        for (size_t j=1; j<n; ++j)",
            "            if (((unsigned char)text[i+j] & 0xC0)!=0x80) return ORPHEUS_ERR_INVALID_ARG;",
            "        unsigned char second=(unsigned char)text[i+1];",
            "        if ((c==0xE0 && second<0xA0) || (c==0xED && second>=0xA0) ||",
            "            (c==0xF0 && second<0x90) || (c==0xF4 && second>=0x90))",
            "            return ORPHEUS_ERR_INVALID_ARG;",
            "        i+=n;", "    }", "    return ORPHEUS_ERR_INVALID_ARG;", "}",
        ]
    lines += [
        f"static int update_parameter({values}* p, const char* key, const OrpheusValue* value) {{",
        "    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;",
    ]
    for p in params:
        key, kind = p["id"], p["type"]
        _, enum, field = _TYPES[kind]
        lines += [f"    if (strcmp(key, {_quote(key)}) == 0) {{"]
        if _is_array(p):
            lines += [
                "        if (value->type!=ORPHEUS_VALUE_STRING || !value->value.str) return ORPHEUS_ERR_INVALID_ARG;",
                "        const char* cursor=value->value.str;",
                f"        for (uint32_t i=0; i<{p['count']}; ++i) {{",
                "            while (*cursor && isspace((unsigned char)*cursor)) ++cursor;",
                "            const char* start=cursor;",
            ]
            if kind == "bool":
                lines += [
                    "            bool element;",
                    '            if (strncmp(cursor,"true",4)==0) { element=true; cursor+=4; }',
                    '            else if (strncmp(cursor,"false",5)==0) { element=false; cursor+=5; }',
                    "            else if (*cursor=='1' || *cursor=='0') { element=(*cursor=='1'); ++cursor; }",
                    "            else return ORPHEUS_ERR_INVALID_ARG;",
                ]
            else:
                c_type = _TYPES[kind][0]
                lines += ["            char* end; errno=0;"]
                if kind == "float":
                    lines += ["            double number=strtod(cursor, &end);",
                              "            if (end==cursor || errno==ERANGE || !isfinite(number) ||",
                              "                number < -3.402823466e38 || number > 3.402823466e38) return ORPHEUS_ERR_INVALID_ARG;"]
                else:
                    lines += ["            long long number=strtoll(cursor, &end, 10);",
                              "            if (end==cursor || errno==ERANGE || number<INT32_MIN || number>INT32_MAX)",
                              "                return ORPHEUS_ERR_INVALID_ARG;"]
                lines += [f"            {c_type} element=({c_type})number;", "            cursor=end;"]
                if "range" in p:
                    lo, hi = (_number(v, kind) for v in p["range"])
                    lines += [f"            if (element<{lo} || element>{hi}) return ORPHEUS_ERR_INVALID_ARG;"]
            lines += [
                "            (void)start;",
                "            while (*cursor && isspace((unsigned char)*cursor)) ++cursor;",
                f"            if (i+1<{p['count']}) {{ if (*cursor!=',') return ORPHEUS_ERR_INVALID_ARG; ++cursor; }}",
                "            else if (*cursor) return ORPHEUS_ERR_INVALID_ARG;",
                f"            p->{key}[i]=element;", "        }",
                "        return ORPHEUS_OK;", "    }",
            ]
            continue
        lines += [f"        if (value->type!=ORPHEUS_VALUE_{enum}) return ORPHEUS_ERR_INVALID_ARG;"]
        if kind == "string":
            lines += [
                "        size_t length;",
                f"        if (valid_utf8_string(value->value.str, sizeof(p->{key}), &length)!=ORPHEUS_OK)",
                "            return ORPHEUS_ERR_INVALID_ARG;",
                f"        char text[sizeof(p->{key})]; memset(text, 0, sizeof(text));",
                "        memcpy(text, value->value.str, length+1);",
                f"        memcpy(p->{key}, text, sizeof(text));",
            ]
        else:
            if kind == "float":
                lines.append("        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;")
            if "range" in p:
                lo, hi = (_number(v, kind) for v in p["range"])
                lines.append(f"        if (value->value.{field}<{lo} || value->value.{field}>{hi}) return ORPHEUS_ERR_INVALID_ARG;")
            if p.get("affects_signature") and "range" not in p:
                lines.append(f"        if (value->value.{field}<1) return ORPHEUS_ERR_INVALID_ARG;")
            lines.append(f"        p->{key}=value->value.{field};")
        lines += ["        return ORPHEUS_OK;", "    }"]
    lines += ["    return ORPHEUS_ERR_NOT_FOUND;", "}"]
    if "set_parameter" in hooks or "get_parameter" in hooks:
        lines += [f"static int parameter_value(const {values}* p, const char* key, OrpheusValue* value) {{",
                  "    (void)p; (void)key; (void)value;"]
        for p in params:
            if _is_array(p):
                continue
            _, enum, field = _TYPES[p["type"]]
            lines += [f"    if (strcmp(key, {_quote(p['id'])})==0) {{",
                      f"        value->type=ORPHEUS_VALUE_{enum}; value->value.{field}=p->{p['id']}; return ORPHEUS_OK;", "    }"]
        lines += ["    return ORPHEUS_ERR_NOT_FOUND;", "}"]
    lines += [
        "static int set_parameter(void* state, const char* key, const OrpheusValue* value) {",
        f"    {state}* s=({state}*)state;",
        "    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;",
    ]
    for p in params:
        if _is_array(p) or p.get("kind") in {"probe", "state"} or p.get("readonly") or p.get("access") == "read":
            lines.append(f"    if (strcmp(key, {_quote(p['id'])})==0) return ORPHEUS_ERR_UNSUPPORTED;")
    lines += [
        f"    {values} before, candidate; snapshot_parameters(s, &before); candidate=before;",
        "    int result=update_parameter(&candidate, key, value);",
        "    if (result!=ORPHEUS_OK) return result;",
    ]
    if definition.get("bulk_bindings"):
        lines += ["    result=validate_resource_counts(&candidate); if (result!=ORPHEUS_OK) return result;"]
    if "set_parameter" in hooks:
        lines += ["    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);",
                  "    if (result!=ORPHEUS_OK) return result;",
                  f"    result={prefix}_set_parameter(s, key, &checked);",
                  "    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }",
                  f"    {values} after; snapshot_parameters(s, &after);",
                  "    result=update_parameter(&after, key, &checked);",
                  "    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }",
                  "    candidate=after;",
                  "    s->_orpheus_cleanup_pending=true;"]
    lines += ["    commit_parameters(s, &candidate); return ORPHEUS_OK;", "}",
              "static int get_parameter(void* state, const char* key, OrpheusValue* value) {",
              f"    {state}* s=({state}*)state;",
              "    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;"]
    if "get_parameter" in hooks:
        lines += [
            f"    {values} before, checked; snapshot_parameters(s, &before); checked=before;",
            "    OrpheusValue candidate={0};",
            f"    int result={prefix}_get_parameter(s, key, &candidate);",
            "    if (result==ORPHEUS_OK) result=update_parameter(&checked, key, &candidate);",
            "    commit_parameters(s, &before);",
            "    if (result!=ORPHEUS_ERR_NOT_FOUND) {",
            "        if (result!=ORPHEUS_OK) return result;",
            "        result=parameter_value(&checked, key, value);",
            "        if (result!=ORPHEUS_OK) return result;",
        ]
        for p in params:
            if _is_array(p):
                continue
            if p["type"] == "string":
                key = p["id"]
                lines += [f"        if (strcmp(key, {_quote(key)})==0) {{",
                          f"            memcpy(s->_orpheus_readback_{key}, checked.{key}, sizeof(checked.{key}));",
                          f"            value->value.str=s->_orpheus_readback_{key};", "        }"]
        lines += ["        return ORPHEUS_OK;", "    }"]
    for p in params:
        if _is_array(p):
            lines += [f"    if (strcmp(key, {_quote(p['id'])})==0) return ORPHEUS_ERR_UNSUPPORTED;"]
            continue
        _, enum, field = _TYPES[p["type"]]
        lines += [f"    if (strcmp(key, {_quote(p['id'])})==0) {{",
                  f"        value->type=ORPHEUS_VALUE_{enum}; value->value.{field}=s->{p['id']}; return ORPHEUS_OK;", "    }"]
    lines += ["    return ORPHEUS_ERR_NOT_FOUND;", "}"]
    lines += [
        f"static int configure_parameters({values}* p, const OrpheusConfig* config) {{",
        "    if (!config || (config->param_count && (!config->param_ids || !config->param_values)))",
        "        return ORPHEUS_ERR_INVALID_ARG;",
        "    default_parameters(p);",
    ]
    if any(p["id"] == "channels" and p["type"] == "int" and not _is_array(p) for p in params):
        lines += [
            "    bool explicit_channels=false;",
            "    for (uint32_t i=0; i<config->param_count; ++i)",
            '        if (config->param_ids[i] && strcmp(config->param_ids[i], "channels")==0) explicit_channels=true;',
            "    if (!explicit_channels && config->channels) {",
            "        if (config->channels>INT32_MAX) return ORPHEUS_ERR_INVALID_ARG;",
            "        OrpheusValue channels={.type=ORPHEUS_VALUE_INT,.value.i32=(int32_t)config->channels};",
            '        int result=update_parameter(p, "channels", &channels);',
            "        if (result!=ORPHEUS_OK) return result;", "    }",
        ]
    lines += [
        "    for (uint32_t i=0; i<config->param_count; ++i) {",
        "        int result=update_parameter(p, config->param_ids[i], &config->param_values[i]);",
        "        if (result!=ORPHEUS_OK) return result;", "    }",
        "    return validate_resource_counts(p);" if definition.get("bulk_bindings") else "    return ORPHEUS_OK;", "}",
        f"static int cleanup({state}* s) {{",
        "    int result=ORPHEUS_OK;",
        "    if (s->_orpheus_cleanup_pending) {",
        "        s->_orpheus_cleanup_pending=false;",
    ]
    if "destroy" in hooks:
        lines.append(f"        result={prefix}_destroy(s);")
    lines += ["        memset(&s->algorithm, 0, sizeof(s->algorithm));", "    }", "    return result;", "}",
              "static int prepare(void* state, const OrpheusConfig* config) {",
              f"    {state}* s=({state}*)state;",
              "    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;",
              f"    {values} candidate; int result=configure_parameters(&candidate, config);",
              "    if (result!=ORPHEUS_OK) return result;",
              "    result=cleanup(s); if (result!=ORPHEUS_OK) return result;",
              "    commit_parameters(s, &candidate);",
              "    s->_orpheus_cleanup_pending=true;"]
    if "prepare" in hooks:
        lines += [f"    result={prefix}_prepare(s, config);",
                  "    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }"]
    lines += ["    return ORPHEUS_OK;", "}",
              "static int create(void** state, const OrpheusConfig* config) {",
              "    if (!state) return ORPHEUS_ERR_INVALID_ARG;",
              "    *state=NULL;",
              "    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;",
              "    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;",
              f"    {values} candidate; int result=configure_parameters(&candidate, config);",
              "    if (result!=ORPHEUS_OK) return result;",
              f"    {state}* s=({state}*)config->state_block;",
              "    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);",
              "    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;", "}",
              "static int destroy(void* state) {",
              f"    {state}* s=({state}*)state;",
              "    if (!s) return ORPHEUS_ERR_INVALID_ARG;",
              "    if (!s->_orpheus_alive) return ORPHEUS_OK;",
              "    int result=cleanup(s); s->_orpheus_alive=false; return result;", "}",
              "static int reset(void* state) {",
              f"    {state}* s=({state}*)state;",
              "    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;"]
    if "reset" in hooks:
        lines += [f"    return {prefix}_reset(s);"]
    else:
        if "destroy" in hooks:
            lines += ["    int result=cleanup(s);",
                      "    memset(&s->algorithm, 0, sizeof(s->algorithm)); return result;"]
        else:
            lines += ["    memset(&s->algorithm, 0, sizeof(s->algorithm)); return ORPHEUS_OK;"]
    lines += ["}", "static int register_slots(void* state, const OrpheusRegistry* reg) {",
              f"    {state}* s=({state}*)state;",
              "    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;"]
    resources = {resource["id"]: resource for resource in definition.get("bulk_bindings", [])}
    registration_helpers = []
    for p in params:
        _, enum, _ = _TYPES[p["type"]]
        resource = resources.get(p["id"])
        if resource:
            enum = _TYPES[resource["type"]][1]
        flags = []
        if p.get("readback", True):
            flags.append("ORPHEUS_SLOT_READBACK")
        if p.get("persistent", True):
            flags.append("ORPHEUS_SLOT_PERSISTENT")
        if p.get("affects_signature"):
            flags.append("ORPHEUS_SLOT_AFFECTS_SIGNATURE")
        if resource and resource["double_bank"]:
            flags.append("ORPHEUS_SLOT_DOUBLE_BUFFERED")
        extra = ""
        bounds = resource.get("range") if resource else p.get("range")
        if bounds:
            kind = resource["type"] if resource else p["type"]
            suffix = "i32" if kind == "int" else "f32"
            extra = "".join(f", .{label}_{suffix}={_number(v, kind)}"
                            for label, v in zip(("min", "max"), bounds))
        if resource:
            key = p["id"]
            slot_kind = p.get("kind", "bulk") if _is_array(p) else "bulk"
            body = [
                f"int {prefix}_register_{key}({state}* s, const OrpheusRegistry* reg) {{",
                "    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;",
                f"    {values} current; snapshot_parameters(s, &current);",
                f"    uint32_t count_{key};",
                f"    if (resource_count_{key}(&current, &count_{key})!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;",
            ]
            registration = f"ORPHEUS_REG_ARRAY(reg, s, {resource['storage']}, count_{key}, ORPHEUS_SLOT_{slot_kind.upper()}, "
        else:
            body = []
            registration = f"ORPHEUS_REG_SLOT(reg, s, {p['id']}, ORPHEUS_SLOT_{p.get('kind', 'setting').upper()}, "
        body += [
            f"    if ({registration}"
            f"{_quote(resource['slot_key'] if resource else p['id'])}, {_quote(p.get('name', p['id']))}, ORPHEUS_VALUE_{enum}, "
            f".update_policy=ORPHEUS_UPDATE_{p['update_policy'].upper()}, .unit={_quote(p.get('unit', ''))}, "
            f".flags={' | '.join(flags) or '0'}{extra})==ORPHEUS_SLOT_ID_INVALID)",
            "        return ORPHEUS_ERR_PROCESSING;",
        ]
        if resource:
            registration_helpers += body + ["    return ORPHEUS_OK;", "}"]
            if resource["auto_register"]:
                lines += [f"    int result_{p['id']}={prefix}_register_{p['id']}(s, reg);",
                          f"    if (result_{p['id']}!=ORPHEUS_OK) return result_{p['id']};"]
        else:
            lines += body
    lines += [f"    return {prefix}_register_slots(s, reg);" if "register_slots" in hooks
              else "    return ORPHEUS_OK;", "}"]
    helper_index = lines.index("static int register_slots(void* state, const OrpheusRegistry* reg) {")
    lines[helper_index:helper_index] = registration_helpers
    if "hook" in hooks:
        lines += [
            "static int hook(void* state, uint32_t id, uint32_t event, const OrpheusBlob* req, OrpheusBlob* resp) {",
            f"    {state}* s=({state}*)state;",
            "    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;",
            f"    return {prefix}_hook(s, id, event, req, resp);", "}",
        ]
    return lines


def _render(definition):
    symbols = definition["algorithm_symbols"]
    prefix, state, io, private, function = (
        symbols[k] for k in ("prefix", "state_type", "io_type", "private_type", "function"))
    upper = prefix.upper()
    ports, params = definition["ports"], definition["parameters"]
    hooks = set(definition.get("algorithm", {}).get("lifecycle", []))
    inputs = [p for p in ports if p["direction"] == "input"]
    outputs = [p for p in ports if p["direction"] == "output"]
    grouped = any("count" in p for p in ports)
    api = ["#pragma once", '#include "orpheus_abi.h"', "",
           "/* input_index、drive_port_no、active 由壳填写，算法不得改写。",
           "   count 组仅 [0, <接口名>_count) 有效；零上界数组保留一个无效占位。 */", ""]
    for c_type, suffix in _FORMATS.values():
        api += [
            f"typedef struct {prefix}_Input{suffix} {{",
            "    const OrpheusBuffer* buffer;", f"    const {c_type}* samples;",
            "    uint32_t input_index;",
            f"}} {prefix}_Input{suffix};",
            f"typedef struct {prefix}_Output{suffix} {{",
            "    OrpheusBuffer* buffer;", f"    {c_type}* samples;", "    OrpheusDelivery delivery;",
            "    uint32_t drive_port_no;", "    bool active;",
            f"}} {prefix}_Output{suffix};", "",
        ]
    if any(p["type"] == "trigger" for p in ports):
        api += [
            f"typedef struct {prefix}_InputTrigger {{",
            "    const OrpheusDelivery* delivery;", "    uint32_t input_index;", f"}} {prefix}_InputTrigger;",
            f"typedef struct {prefix}_OutputTrigger {{",
            "    OrpheusDelivery delivery;", "    uint32_t drive_port_no;", "    bool active;",
            f"}} {prefix}_OutputTrigger;", "",
        ]
    if inputs and not grouped:
        api.append("enum {")
        api.extend(f"    {upper}_INPUT_{p['id'].upper()} = {i}," for i, p in enumerate(inputs))
        api.append("};")
    driven = [p for p in outputs if "drive_port_no" in p]
    # UINT32_MAX-1 超过 C11 enum 的 int 范围，使用强类型常量宏。
    api.extend(f"#define {upper}_DRIVE_{p['id'].upper()}{'_BASE' if 'count' in p else ''} "
               f"UINT32_C({p['drive_port_no']})" for p in driven)
    api += [f"typedef struct {io} {{"]
    for port in ports:
        array = f"[{max(1, port['count_max'])}]" if "count" in port else ""
        api.append(f"    {prefix}_{'Input' if port['direction'] == 'input' else 'Output'}"
                   f"{_port_suffix(port)} {port['id']}{array};")
        if "count" in port:
            api.append(f"    uint32_t {port['id']}_count;")
    if not ports:
        api.append("    unsigned char reserved;")
    api += [f"}} {io};", ""]
    for port in driven:
        name = port["id"]
        end = port["drive_port_no"] + port.get("count_max", 1) - 1
        api += [
            f"static inline int {prefix}_emit_{name}(const OrpheusProcessContext* ctx,",
            f"        const {prefix}_Output{_port_suffix(port)}* output) {{",
            "    if (!ctx || !output || !output->active || output->delivery.buffer != " +
            ("NULL" if port["type"] == "trigger" else "output->buffer"),
            f"            || output->drive_port_no < UINT32_C({port['drive_port_no']})",
            f"            || output->drive_port_no > UINT32_C({end}))",
            "        return ORPHEUS_ERR_INVALID_ARG;",
            "    return orpheus_emit(ctx, output->drive_port_no, &output->delivery);", "}",
        ]
    state_header = [
        "#pragma once", '#include "component_api.h"', '#include "../user/algorithm.h"', "",
        f"typedef struct {state} {{",
    ]
    state_header.extend(f"    {_parameter_declaration(p)}" for p in params)
    if "get_parameter" in hooks:
        state_header.extend(f"    char _orpheus_readback_{p['id']}[{p.get('capacity', 256)}];"
                            for p in params if p["type"] == "string")
    state_alignment = definition.get("memory", {}).get("alignment", 1)
    # 对齐布尔成员，避免声明值低于 AlgorithmState 自然对齐时 C/C++ 拒绝降级。
    state_header += ["#if defined(__cplusplus)",
                     f"    alignas({state_alignment}) bool _orpheus_alive;",
                     "#else", f"    _Alignas({state_alignment}) bool _orpheus_alive;", "#endif",
                     "    bool _orpheus_cleanup_pending;",
                     f"    {private} algorithm;", f"}} {state};", "",
                     "#if defined(__cplusplus)", 'extern "C" {', "#endif",
                     f"int {function}({state}* state, {io}* io, const OrpheusProcessContext* ctx);"]
    signatures = _lifecycle_signatures(state)
    if hooks:
        state_header += [
            "/* create 不调用用户 prepare；prepare 应用配置后调用用户入口，失败自动 destroy 收尾。",
            "   set 先校验，再以旧 State 调用户 hook，成功提交当前字段，失败恢复生成参数。",
            "   get 先调用户 hook，仅 NOT_FOUND 回退字段读取；用户必须填写返回值的 type。",
            "   reset 入口可更新运行状态；未声明时清空 algorithm（有 destroy 时先收尾）。 */",
        ]
    state_header.extend(f"int {prefix}_{hook}({signatures[hook]});" for hook in LIFECYCLE_HOOKS if hook in hooks)
    state_header.extend(
        f"int {prefix}_register_{resource['id']}({state}* state, const OrpheusRegistry* reg);"
        for resource in definition.get("bulk_bindings", []))
    state_header += ["#if defined(__cplusplus)", "}", "#endif"]
    user_header = [
        "#pragma once", "#include <stdint.h>", "#include <stdbool.h>", "",
        "/* 用户私有状态可在此扩展；实时处理不分配内存。 */",
        f"typedef struct {private} {{", *_private_resource_fields(definition), f"}} {private};",
    ]
    user_source = [
        '#include "component_state.h"', '#include "component_api.h"', "",
        f"int {function}({state}* state, {io}* io, const OrpheusProcessContext* ctx) {{",
        "    (void)state; (void)io; (void)ctx;",
        "    /* 请实现算法，并在数据就绪后显式调用对应 emit 函数。 */",
        "    return ORPHEUS_ERR_UNSUPPORTED;", "}",
    ]
    hook_arguments = {
        "prepare": ("state", "config"), "reset": ("state",), "destroy": ("state",),
        "set_parameter": ("state", "key", "value"), "get_parameter": ("state", "key", "value"),
        "register_slots": ("state", "reg"), "hook": ("state", "id", "event", "req", "resp"),
    }
    for hook in LIFECYCLE_HOOKS:
        if hook in hooks:
            user_source += ["", f"int {prefix}_{hook}({signatures[hook]}) {{"]
            user_source += [f"    (void){arg};" for arg in hook_arguments[hook]]
            result = ("ORPHEUS_HOOK_CONTINUE" if hook == "hook" else
                      "ORPHEUS_ERR_NOT_FOUND" if hook == "get_parameter" else "ORPHEUS_OK")
            user_source += [f"    return {result};", "}"]
    shell = [
        '#include "component_state.h"', "#include <string.h>", "#include <math.h>", "",
    ]
    if any(_is_array(p) for p in params):
        shell += ["#include <stdlib.h>", "#include <errno.h>", "#include <ctype.h>", ""]
    for resource in definition.get("bulk_bindings", []):
        c_type = _TYPES[resource["type"]][0]
        member = f"(({state}*)0)->{resource['storage']}"
        shell += [
            f'_Static_assert(_Generic(&({member}), {c_type} (*)[{resource["count"]}]: 1, default: 0),',
            f'    "BULK {resource["id"]}: storage 类型或固定容量不匹配");',
        ]
    by_id = {p["id"]: p for p in ports}

    def expression(port, field, context=False):
        value = port.get(field, 0)
        if isinstance(value, int):
            return str(value)
        target, attr = value.split(":")
        if target == "param":
            return f"s->{attr}" if context else "0"
        if target == "task":
            return f"ctx->{'frame_count' if attr == 'block_size' else 'sample_rate'}" if context else "0"
        return expression(by_id[target], attr, context)

    if ports:
        shell.append("static const OrpheusPort ports[] = {")
        for port in ports:
            variable_channels = isinstance(port.get("channels"), str)
            variable = variable_channels or isinstance(port.get("count"), str)
            channel_param = port["channels"].split(":")[1] if variable_channels and port["channels"].startswith("param:") else None
            shell.append(
                f"    {{.id={_quote(port['id'])}, .direction=ORPHEUS_PORT_{port['direction'].upper()}, "
                f".type=ORPHEUS_PORT_{port['type'].upper()}, .sample_format=ORPHEUS_FORMAT_{port.get('sample_format', 'f32').upper()}, "
                f".channels={expression(port, 'channels')}, .sample_rate={expression(port, 'sample_rate')}, "
                f".block_size={expression(port, 'block_size')}, .is_variable={'true' if variable else 'false'}, "
                f".channels_param={_quote(channel_param) if channel_param else 'NULL'}}},")
        shell += ["};", ""]
    if params:
        shell.append("static const OrpheusParameter params[] = {")
        for param in params:
            _, enum, field = _TYPES[param["type"]]
            default_enum = "STRING" if _is_array(param) else enum
            default_field = "str" if _is_array(param) else field
            default_value = _quote(_csv_default(param)) if _is_array(param) else _number(param["default"], param["type"])
            bounds = param.get("range")
            bounds_code = ""
            if bounds:
                suffix = "i32" if param["type"] == "int" else "f32"
                bounds_code = f", .min_{suffix}={_number(bounds[0], param['type'])}, .max_{suffix}={_number(bounds[1], param['type'])}"
            shell.append(
                f"    {{.id={_quote(param['id'])}, .name={_quote(param.get('name', param['id']))}, "
                f".type=ORPHEUS_VALUE_{enum}, .default_value={{.type=ORPHEUS_VALUE_{default_enum}, "
                f".value.{default_field}={default_value}}}, "
                f".update_policy=ORPHEUS_UPDATE_{param['update_policy'].upper()}, "
                f".unit={_quote(param.get('unit', ''))}, "
                f".readback={str(param.get('readback', True)).lower()}, "
                f".persistent={str(param.get('persistent', True)).lower()}, "
                f".affects_signature={str(param.get('affects_signature', False)).lower()}{bounds_code}}},")
        shell += ["};", ""]
    execution, memory = definition.get("execution", {}), definition.get("memory", {})
    shell += [
        "static const OrpheusComponentDescriptor descriptor = {",
        f"    .id={_quote(definition['id'])}, .version={_quote(definition['version'])}, .abi_version=7,",
        f"    .ports={'ports' if ports else 'NULL'}, .port_count={len(ports)},",
        f"    .params={'params' if params else 'NULL'}, .param_count={len(params)},",
        f"    .state_size=sizeof({state}), .alignment=_Alignof({state}),",
        f"    .scratch_size={memory.get('scratch_size', 0)}, .latency_samples={execution.get('latency_samples', 0)},",
        f"    .realtime_safe={str(execution.get('realtime_safe', True)).lower()}, "
        f".supports_inplace={str(execution.get('supports_inplace', False)).lower()}",
        "};",
        "static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }", "",
    ]
    shell += _render_parameter_shell(definition)
    shell += ["static int valid_buffer(const OrpheusBuffer* buffer, uint32_t channels,",
              "        OrpheusSampleFormat format, size_t sample_size, size_t alignment) {",
              "    return !buffer || (buffer->format == format && buffer->interleaved",
              "        && buffer->channels == channels && channels > 0",
              "        && buffer->frame_count <= buffer->frame_capacity",
              "        && (!buffer->frame_capacity || buffer->data)",
              "        && (!buffer->data || (uintptr_t)buffer->data % alignment == 0)",
              "        && (size_t)buffer->frame_capacity <= SIZE_MAX / sample_size / channels);", "}",
              "static int process(void* state, const OrpheusProcessContext* ctx) {",
              f"    {state}* s=({state}*)state;", f"    {io} io={{0}};",
              "    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;"]
    if grouped:
        shell += ["    uint32_t _orpheus_input_count=0, _orpheus_output_count=0;"]
        for port in ports:
            direction, name = port["direction"], port["id"]
            if "count" in port:
                count = f"s->{port['count_param']}" if "count_param" in port else str(port["count"])
                shell += [
                    f"    int64_t _orpheus_count_{name}={count};",
                    f"    if (_orpheus_count_{name} < {port['count_min']} || _orpheus_count_{name} > {port['count_max']})",
                    "        return ORPHEUS_ERR_INVALID_ARG;",
                    f"    io.{name}_count=(uint32_t)_orpheus_count_{name};",
                ]
            amount = f"io.{name}_count" if "count" in port else "1"
            shell.append(f"    _orpheus_{direction}_count+={amount};")
        shell += [
            "    if (ctx->input_count!=_orpheus_input_count || ctx->output_count!=_orpheus_output_count)",
            "        return ORPHEUS_ERR_INVALID_ARG;",
            "    if (ctx->trigger_input!=UINT32_MAX && ctx->trigger_input>=_orpheus_input_count)",
            "        return ORPHEUS_ERR_INVALID_ARG;",
            "    if (ctx->delivery && ctx->trigger_input==UINT32_MAX) return ORPHEUS_ERR_INVALID_ARG;",
        ]
    for group, direction in ((inputs, "input"), (outputs, "output")):
        if grouped and group:
            shell.append(f"    uint32_t _orpheus_{direction}_slot=0;")
        for index, port in enumerate(group):
            name = port["id"]
            is_group = "count" in port
            flat_index = f"_orpheus_{direction}_slot" if grouped else str(index)
            if is_group:
                shell.append(f"    for (uint32_t _orpheus_i=0; _orpheus_i<io.{name}_count; ++_orpheus_i) {{")
                flat_index += "+_orpheus_i"
            target = f"io.{name}[_orpheus_i]" if is_group else f"io.{name}"
            if direction == "input":
                shell.append(f"    {target}.input_index={flat_index};")
            else:
                drive = f"{port['drive_port_no']}u" if "drive_port_no" in port else "0"
                if is_group and "drive_port_no" in port:
                    drive += "+_orpheus_i"
                shell += [f"    {target}.active=true;", f"    {target}.drive_port_no={drive};"]
            if port["type"] == "trigger":
                shell += [
                    f"    if (ctx->{direction}s && ctx->{direction}_count>({flat_index}) && ctx->{direction}s[{flat_index}])",
                    "        return ORPHEUS_ERR_INVALID_ARG;",
                ]
                if direction == "input":
                    shell += [
                        f"    {target}.delivery=ctx->trigger_input==({flat_index}) ? ctx->delivery : NULL;",
                        f"    if ({target}.delivery && {target}.delivery->buffer) return ORPHEUS_ERR_INVALID_ARG;",
                    ]
                else:
                    shell += [
                        f"    {target}.delivery=(OrpheusDelivery){{.buffer=NULL,.frame_index=ctx->frame_index,",
                        "        .epoch=ctx->epoch,.timeline_flags=ctx->timeline_flags};",
                    ]
            else:
                c_type, _ = _FORMATS[port["sample_format"]]
                shell += [
                    f"    {target}.buffer=(ctx->{direction}s && ctx->{direction}_count > ({flat_index})) ? ctx->{direction}s[{flat_index}] : NULL;",
                    f"    if (!valid_buffer({target}.buffer, (uint32_t)({expression(port, 'channels', True)}),",
                    f"        ORPHEUS_FORMAT_{port['sample_format'].upper()}, sizeof({c_type}), _Alignof({c_type})))",
                    "        return ORPHEUS_ERR_INVALID_ARG;",
                    f"    {target}.samples={target}.buffer ? ({'const ' if direction == 'input' else ''}{c_type}*){target}.buffer->data : NULL;",
                ]
                if direction == "output":
                    shell += [
                        f"    if ({target}.buffer) {{",
                        f"        if (ctx->frame_count > {target}.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;",
                        f"        {target}.buffer->frame_count=ctx->frame_count;", "    }",
                        f"    {target}.delivery=(OrpheusDelivery){{.buffer={target}.buffer, .frame_index=ctx->frame_index,",
                        "        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};",
                    ]
            if is_group:
                shell.append("    }")
            if grouped:
                amount = f"io.{name}_count" if is_group else "1"
                shell.append(f"    _orpheus_{direction}_slot+={amount};")
    shell += [f"    return {function}(s, &io, ctx);", "}",
              "static const OrpheusComponentInterface interface = {",
              "    .get_descriptor=get_descriptor, .create=create, .destroy=destroy,",
              "    .prepare=prepare, .reset=reset, .process=process,",
              "    .set_parameter=set_parameter, .get_parameter=get_parameter, .register_slots=register_slots,",
              f"    .hook={'hook' if 'hook' in hooks else 'NULL'}",
              "};", "#ifndef ORPHEUS_ENTRY_NAME", "#define ORPHEUS_ENTRY_NAME orpheus_get_interface",
              "#endif", "ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }"]
    if not any(p["type"] != "trigger" for p in ports):
        start = shell.index("static int valid_buffer(const OrpheusBuffer* buffer, uint32_t channels,")
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
