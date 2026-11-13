"""独立组件声明的只读加载、校验和内部视图派生。"""

from __future__ import annotations

import copy
import json
import math
import re
import struct
from fractions import Fraction
from pathlib import Path

import yaml


class ComponentDefinitionError(ValueError):
    """可供 CLI 和构建工具消费的稳定诊断。"""

    def __init__(self, message: str, location: str = "$", code: str = "invalid_definition"):
        super().__init__(f"{location}: {message}")
        self.diagnostic = {"code": code, "location": location, "message": message}


class _UniqueLoader(yaml.SafeLoader):
    pass


def _mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if not isinstance(key, str):
            raise ComponentDefinitionError("映射键必须是字符串", f"行{key_node.start_mark.line + 1}")
        if key in result:
            raise ComponentDefinitionError(f"重复键：{key}", f"行{key_node.start_mark.line + 1}", "duplicate_key")
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


_UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _mapping)
_IDENTIFIER = re.compile(r"[A-Za-z][A-Za-z0-9_]*\Z")
_KEYWORDS = set(("auto break case char const continue default do double else enum extern float for goto "
                 "if inline int long register restrict return short signed sizeof static struct switch "
                 "typedef union unsigned void volatile while bool true false class namespace template "
                 "private public protected new delete operator NULL").split())
_POLICIES = {"immediate", "block_boundary", "smoothed", "transactional", "restart_required"}
F32_MAX = float.fromhex("0x1.fffffep+127")
_SIGNATURE_PRODUCT = re.compile(
    r"(?:param:[A-Za-z][A-Za-z0-9_]*|[1-9][0-9]*)(?:\*(?:param:[A-Za-z][A-Za-z0-9_]*|[1-9][0-9]*))+")
_RATE_SCALE = re.compile(
    r"(?:in|task):sample_rate(?:[*/](?:param:[A-Za-z][A-Za-z0-9_]*|[1-9][0-9]*))+")
_PAYLOAD_RATIO = re.compile(
    r"(?:param:[A-Za-z][A-Za-z0-9_]*|[1-9][0-9]*)(?:[*/](?:param:[A-Za-z][A-Za-z0-9_]*|[1-9][0-9]*))+")
LIFECYCLE_HOOKS = ("normalize", "validate", "prepare", "reset", "destroy", "set_parameter", "get_parameter", "register_slots", "hook")
_TOP = {"id", "name", "version", "abi_version", "package_type", "clock_source", "clock_domain",
        "category", "description", "interfaces", "algorithm", "order", "execution", "memory",
        "platforms", "ui_hidden", "user_owned", "scheduling", "input_alternatives", "deps",
        "codegen_template"}
_PORT = {"name", "direction", "type", "sample_format", "channels", "block_size", "sample_rate",
         "process", "drive_port_no", "drive_divide", "count", "description", "layout", "frame_size",
         "timing", "optional", "allow_unready", "host_binding"}
_PARAM = {"name", "type", "default", "range", "update_policy", "affects_signature", "readback",
          "persistent", "unit", "count", "shape", "capacity", "bindable", "control_source",
          "kind", "description", "widget", "options", "options_source", "readonly", "file_ext",
          "access", "bulk_slot", "bulk"}


def _fail(message, location="$", code="invalid_definition"):
    raise ComponentDefinitionError(message, location, code)


def is_array_parameter(parameter: dict) -> bool:
    """新接口中显式 count 的数值参数都是固定数组，包括 count=1。"""
    return parameter.get("type") in ("float", "int", "bool") and "count" in parameter


def _valid_numeric_element(kind, value):
    if kind == "bool":
        return type(value) is bool
    if kind == "int":
        return type(value) is int and -2147483648 <= value <= 2147483647
    return type(value) in (float, int) and -F32_MAX <= value <= F32_MAX


def encode_array_initializer(parameter: dict, value: object, *, location: str | None = None) -> str:
    """严格校验公开列表值并编码 ABI STRING CSV；不接受预编码字符串，也不修改输入。"""
    if not isinstance(parameter, dict):
        _fail("数组参数声明必须是映射", location or "$")
    location = location or f"parameters.{parameter.get('id', 'array')}"
    if not is_array_parameter(parameter):
        _fail("仅支持显式声明 count 的 float/int/bool 固定数组", location)
    count, kind = parameter["count"], parameter["type"]
    if type(count) is not int or not 1 <= count <= 65536:
        _fail("数组 count 必须是 1..65536 的固定整数", location + ".count")
    bounds = parameter.get("range")
    if "range" in parameter and (
            kind == "bool" or not isinstance(bounds, list) or len(bounds) != 2 or
            not all(_valid_numeric_element(kind, v) for v in bounds) or bounds[0] > bounds[1]):
        _fail("数组 range 必须是元素类型范围内的有序有限数值区间", location + ".range")
    if not isinstance(value, list) or len(value) != count:
        _fail("数组初始值必须是恰好 count 个同类型元素的列表", location)
    encoded = []
    for index, element in enumerate(value):
        if not _valid_numeric_element(kind, element):
            _fail("数组元素类型或数值范围非法（float/int 不接受 bool）", f"{location}[{index}]")
        if bounds is not None and not bounds[0] <= element <= bounds[1]:
            _fail("数组元素不在声明的 range 中", f"{location}[{index}]")
        encoded.append(("1" if element else "0") if kind == "bool" else
                       str(float(element)) if kind == "float" else str(element))
    return ",".join(encoded)


def _parameter_default(parameter):
    if "default" in parameter:
        return parameter["default"]
    zero = {"float": 0.0, "int": 0, "bool": False, "string": ""}[parameter["type"]]
    return [zero] * parameter["count"] if is_array_parameter(parameter) else zero


def _identifier(value, location):
    if not isinstance(value, str) or not _IDENTIFIER.fullmatch(value) or value in _KEYWORDS:
        _fail("必须是非保留的 C 标识符（字母开头）", location)


def component_symbols(raw: dict) -> dict:
    algorithm = raw.get("algorithm", {})
    if not isinstance(algorithm, dict):
        _fail("算法配置必须是映射", "algorithm")
    prefix = algorithm.get("prefix", re.sub(r"[.-]", "_", raw["id"]))
    result = {"prefix": prefix, "state_type": f"{prefix}_State",
              "io_type": f"{prefix}_IO", "private_type": f"{prefix}_AlgorithmState",
              "function": f"{prefix}_algorithm"}
    result.update({k: v for k, v in algorithm.items() if k != "lifecycle"})
    return result


def _string(value, location, nonempty=False):
    if not isinstance(value, str) or "\0" in value or (nonempty and not value):
        _fail("必须是有效字符串且不能含 NUL", location)
    try:
        value.encode("utf-8")
    except UnicodeError:
        _fail("字符串必须能编码为 UTF-8", location)


def _metadata(raw):
    if "deps" in raw:
        dependencies = raw["deps"]
        if not isinstance(dependencies, list):
            _fail("依赖必须是唯一名称数组", "deps")
        for index, value in enumerate(dependencies):
            if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*(?:[.-][A-Za-z0-9_]+)*", value):
                _fail("依赖必须是组件ID或库名称", f"deps[{index}]")
        if len(set(dependencies)) != len(dependencies) or raw["id"] in dependencies:
            _fail("依赖不能重复或引用组件自身", "deps")
    for key in ("clock_source", "ui_hidden", "user_owned"):
        if key in raw and type(raw[key]) is not bool:
            _fail("必须是布尔值", key)
    if "order" in raw and type(raw["order"]) is not int:
        _fail("排序必须是整数", "order")
    if "platforms" in raw:
        if not isinstance(raw["platforms"], list):
            _fail("平台限制必须是字符串数组", "platforms")
        for value in raw["platforms"]:
            _string(value, "platforms", True)
    for section, allowed in (
        ("execution", {"none", "sample_rate_independent", "latency_samples", "supports_inplace", "realtime_safe"}),
        ("memory", {"state_size", "scratch_size", "alignment"}),
    ):
        data = raw.get(section, {})
        if not isinstance(data, dict) or set(data) - allowed:
            _fail("存在未知字段或不是映射", section)
        for key, value in data.items():
            if section == "execution" and key != "latency_samples":
                if type(value) is not bool:
                    _fail("必须是布尔值", f"{section}.{key}")
            elif type(value) is not int or not 0 <= value <= 4294967294:
                _fail("必须是非负 32 位整数", f"{section}.{key}")
        if section == "memory" and "alignment" in data:
            alignment = data["alignment"]
            if not alignment or alignment & (alignment - 1):
                _fail("alignment 必须是正的 2 次幂", "memory.alignment")
    scheduling = raw.get("scheduling", {})
    if not isinstance(scheduling, dict) or set(scheduling) - {"history", "split", "merge", "async_bridge", "divisor", "input_block_size"}:
        _fail("scheduling 只能声明已知的调度字段", "scheduling")
    for key, value in scheduling.items():
        if key in {"history", "split", "merge", "async_bridge"}:
            if type(value) is not bool:
                _fail("必须是布尔值", f"scheduling.{key}")
        elif type(value) is not int or value < 1:
            if not isinstance(value, str) or not re.fullmatch(
                    r"(?:[1-9][0-9]*|param:[A-Za-z][A-Za-z0-9_]*)(?:\s*\*\s*(?:[1-9][0-9]*|param:[A-Za-z][A-Za-z0-9_]*))*", value):
                _fail("调度量只能是正整数、param 引用或它们的乘积", f"scheduling.{key}")


def component_bulk_resources(interfaces: dict) -> list[dict]:
    """派生 BULK 绑定；id 是 C 接口名，slot_key 是外部资源键，默认同名。"""
    resources = []
    for key, item in interfaces.items():
        if "direction" in item:
            continue
        is_array = is_array_parameter(item)
        binding = item.get("bulk")
        if binding is None and not is_array:
            continue
        binding = binding or {}
        resource_type = {"f32": "float", "i32": "int", "bool": "bool"}.get(
            binding.get("sample_format"), item["type"])
        count = item["count"] if is_array else binding["capacity"]
        resource = {
            "id": key, "name": item.get("name", key), "type": resource_type, "count": count,
            "slot_key": binding.get("slot_key", key),
            "shape": copy.deepcopy(item.get("shape", [count])),
            "double_bank": binding.get("double_bank", False),
            "auto_register": binding.get("auto_register", True),
            "access": item.get("access", "read" if item.get("kind") in {"probe", "state"} or item.get("readonly") else "read_write"),
            "storage": key if is_array else binding["storage"],
        }
        if "unit" in item:
            resource["unit"] = item["unit"]
        bounds = binding.get("range", item.get("range"))
        if bounds is not None:
            resource["range"] = copy.deepcopy(bounds)
        if "when" in binding:
            resource["when"] = copy.deepcopy(binding["when"])
        resources.append(resource)
    return resources


def validate_configuration_conditions(conditions: dict, parameters: dict, location: str,
                                      label: str) -> None:
    if not isinstance(conditions, dict):
        _fail(f"{label} when 必须是配置参数到标量值的映射", location)
    for key, value in conditions.items():
        parameter = parameters.get(key) if isinstance(key, str) else None
        if (parameter is None or parameter.get("update_policy") != "restart_required"
                or is_array_parameter(parameter) or "bulk" in parameter
                or parameter.get("kind") in {"probe", "state"} or "direction" in parameter):
            _fail(f"{label}条件须引用重启生效的标量配置参数：{key}", location)
        kind = parameter["type"]
        if kind == "string":
            _string(value, location)
            valid = len(value.encode("utf-8")) < parameter.get("capacity", 256)
        else:
            valid = _valid_numeric_element(kind, value)
        if not valid or "range" in parameter and not parameter["range"][0] <= value <= parameter["range"][1]:
            _fail(f"{label}条件值不符合配置参数类型或范围：{key}", location)


def configuration_conditions_match(conditions: dict, parameters: dict, values: dict) -> bool:
    for key, expected in conditions.items():
        actual = values[key]
        if parameters[key]["type"] == "float":
            if not _valid_numeric_element("float", actual):
                _fail(f"条件配置必须是有效f32标量：{key}", f"params.{key}")
            actual = struct.unpack("<f", struct.pack("<f", actual))[0]
            expected = struct.unpack("<f", struct.pack("<f", expected))[0]
        if actual != expected:
            return False
    return True


def validate_host_bindings(ports: list[dict], parameters: list[dict], resources: list[dict],
                           *, location: str = "ports") -> None:
    """独立声明工具与图编译共用宿主端口的静态存储契约。"""
    slots = {resource.get("slot_key", resource["id"]): resource for resource in resources}
    statuses = {parameter["id"]: parameter for parameter in parameters}
    for port in ports:
        if "host_binding" not in port:
            continue
        loc = f"{location}.{port['id']}.host_binding"
        binding = port["host_binding"]
        if (not isinstance(binding, dict) or set(binding) - {"slot", "access", "status", "when"}
                or not {"slot", "access", "status"} <= set(binding)):
            _fail("宿主绑定必须声明 slot/access/status，且不能包含未知字段", loc)
        if port.get("type") != "audio" or port.get("sample_format", "f32") != "f32":
            _fail("宿主端口当前只支持交错 f32 音频", loc)
        for key in ("slot", "status"):
            _string(binding[key], loc + "." + key, True)
        access = binding["access"]
        if access not in ("write", "read"):
            _fail("宿主访问方向必须是 write/read", loc + ".access")
        slot = slots.get(binding["slot"])
        if slot is None or slot.get("type", "float") != "float":
            _fail("宿主端口缺少 float BULK 存储声明", loc + ".slot")
        permitted = ("write", "read_write") if access == "write" else ("read", "read_write")
        if slot.get("access", "read_write") not in permitted:
            _fail(f"宿主端口的存储访问权限不允许 {access}", loc + ".slot")
        status = statuses.get(binding["status"])
        if status is None or status["type"] != "bool" or is_array_parameter(status):
            _fail("宿主端口缺少 bool 标量交付状态声明", loc + ".status")
        status_access = status.get(
            "access", "read" if status.get("kind") in {"probe", "state"} or status.get("readonly") else "read_write")
        if access == "write" and (status_access != "read_write" or status.get("readonly")):
            _fail("宿主端口的提交状态不可写或不可读", loc + ".status")
        if access == "read" and status_access not in ("read", "read_write"):
            _fail("宿主端口的有效状态不可读", loc + ".status")
        validate_configuration_conditions(binding.get("when", {}), statuses, loc + ".when", "宿主端口")


def _port_count_bounds(key, port, interfaces):
    """常数时间求组的下界、上界和默认值，不展开任何端口。"""
    value = port.get("count", 1)
    lower = 0 if port.get("optional", False) else 1
    location = f"interfaces.{key}.count"
    if type(value) is int:
        if not lower <= value <= 4294967294:
            _fail("count 必须在 1..UINT32_MAX-1；optional:true 时允许 0", location)
        return value, value, value
    if not isinstance(value, str) or not re.fullmatch(r"param:[A-Za-z][A-Za-z0-9_]*", value):
        _fail("count 仅支持固定整数或 param:<参数名>，不接受其它签名表达式", location)
    parameter = interfaces.get(value.split(":")[1], {})
    if (parameter.get("type") != "int" or is_array_parameter(parameter) or
            parameter.get("update_policy") != "restart_required"):
        _fail("count 引用必须指向 restart_required 的 int 标量参数", location)
    bounds = parameter.get("range")
    if (not isinstance(bounds, list) or len(bounds) != 2 or
            any(type(v) is not int for v in bounds) or
            not lower <= bounds[0] <= bounds[1] <= 2147483647):
        _fail("count 参数须有有限整数 range，非 optional 组的下界至少为 1", location)
    default = _parameter_default(parameter)
    if type(default) is not int or not bounds[0] <= default <= bounds[1]:
        _fail("count 参数默认值必须是 range 内的整数", location)
    return bounds[0], bounds[1], default


def _expanded_index_exists(suffix, maximum):
    if not re.fullmatch(r"0|[1-9][0-9]*", suffix):
        return False
    bound = str(maximum)
    return len(suffix) < len(bound) or (len(suffix) == len(bound) and suffix < bound)


def _validate_port_groups(interfaces):
    groups, intervals = [], []
    drive_symbols = set()
    maximum_slots = {"input": 0, "output": 0}
    for key, port in interfaces.items():
        if "direction" not in port:
            continue
        _, maximum, _ = _port_count_bounds(key, port, interfaces)
        maximum_slots[port["direction"]] += maximum
        if maximum_slots[port["direction"]] > 4294967295:
            _fail("展开后的最大槽位数超出 uint32 范围", f"interfaces.{key}.count")
        if "count" in port:
            groups.append((key, maximum))
            if key + "_count" in interfaces and "direction" in interfaces[key + "_count"]:
                _fail("端口名称与生成的组 count 字段冲突", f"interfaces.{key}.count")
        if "drive_port_no" in port and maximum:
            start = port["drive_port_no"]
            end = start + maximum - 1
            if end > 4294967294:
                _fail("驱动编号预留区间超出 UINT32_MAX-1", f"interfaces.{key}.drive_port_no")
            intervals.append((start, end, key))
        if "drive_port_no" in port:
            symbol = key.upper() + ("_BASE" if "count" in port else "")
            if symbol in drive_symbols:
                _fail("驱动出口常量名称冲突", f"interfaces.{key}.drive_port_no")
            drive_symbols.add(symbol)
    intervals.sort()
    for previous, current in zip(intervals, intervals[1:]):
        if current[0] <= previous[1]:
            _fail("驱动编号预留区间重复或重叠（按最大 count 校验）",
                  f"interfaces.{current[2]}.drive_port_no")
    group_names = {key for key, _ in groups}
    for key, maximum in groups:
        if not maximum:
            continue
        for other in interfaces:
            if other not in group_names and other.startswith(key) and _expanded_index_exists(other[len(key):], maximum):
                _fail("count 展开后的接口 ID 与固定接口冲突", f"interfaces.{key}.count")
        for other, other_maximum in groups:
            if (other != key and other_maximum and other.startswith(key) and
                    _expanded_index_exists(other[len(key):] + "0", maximum)):
                _fail("count 组展开后的接口 ID 相互冲突", f"interfaces.{key}.count")


def _validate_input_alternatives(groups, interfaces):
    if not isinstance(groups, list):
        _fail("input_alternatives 必须是输入接口组的列表", "input_alternatives")
    seen = set()
    for index, group in enumerate(groups):
        location = f"input_alternatives[{index}]"
        if not isinstance(group, list) or len(group) < 2:
            _fail("每组必须是至少两个不同输入接口键的列表", location)
        for position, key in enumerate(group):
            member_location = f"{location}[{position}]"
            if not isinstance(key, str) or key not in interfaces:
                _fail("必须引用已声明的 interfaces 输入接口键", member_location)
            port = interfaces[key]
            if (port.get("direction") != "input" or
                    port.get("type") not in {"audio", "bulk", "control", "debug"} or "count" in port):
                _fail("仅允许未声明 count 的单个输入数据接口，不允许输出、参数或纯触发接口", member_location)
            if key in seen:
                _fail("input_alternatives 的接口不得在同组或不同组中重复、交叠", member_location)
            seen.add(key)


def _validate(raw):
    if not isinstance(raw, dict):
        _fail("组件声明必须是映射")
    unknown = set(raw) - _TOP
    if unknown:
        _fail(f"不允许的顶层字段：{', '.join(sorted(map(str, unknown)))}；只能以 interfaces 声明接口")
    for key in ("id", "name", "version", "interfaces"):
        if key not in raw:
            _fail("缺少必需字段", key)
    if not isinstance(raw["id"], str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*(?:[.-][A-Za-z0-9_]+)*", raw["id"]):
        _fail("组件 ID 非法", "id")
    for key in ("name", "version", "category", "description", "clock_domain"):
        if key not in raw:
            continue
        _string(raw[key], key, True)
    if "abi_version" in raw and (type(raw["abi_version"]) is not int or raw["abi_version"] != 7):
        _fail("此工具仅支持 ABI v7", "abi_version")
    if raw.get("package_type", "source") != "source":
        _fail("此工具仅支持 source 组件包", "package_type")
    _metadata(raw)
    algorithm = raw.get("algorithm", {})
    if not isinstance(algorithm, dict) or set(algorithm) - {"prefix", "state_type", "io_type", "private_type", "function", "lifecycle"}:
        _fail("算法配置只允许符号覆盖及显式 lifecycle 列表", "algorithm")
    lifecycle = algorithm.get("lifecycle", [])
    if (not isinstance(lifecycle, list) or any(not isinstance(h, str) or h not in LIFECYCLE_HOOKS for h in lifecycle)
            or len(set(lifecycle)) != len(lifecycle)):
        _fail("lifecycle 必须是无重复的已知生命周期名称列表", "algorithm.lifecycle")
    for key, value in component_symbols(raw).items():
        _identifier(value, f"algorithm.{key}")
    symbols = component_symbols(raw)
    if len(set(symbols.values())) != len(symbols):
        _fail("算法类型和符号不能重名", "algorithm")
    reserved_symbols = {
        "create", "destroy", "prepare", "reset", "process", "set_parameter", "get_parameter",
        "register_slots", "get_descriptor", "valid_buffer", "interface", "descriptor", "ports", "params",
        "cleanup", "snapshot_parameters", "commit_parameters", "default_parameters", "update_parameter",
        "parameter_value", "configure_parameters", "normalize_parameters", "validate_resource_counts", "valid_utf8_string",
        "orpheus_get_interface", "orpheus_emit", "orpheus_process_next", "orpheus_process_branch",
        f"{symbols['prefix']}_InputF32", f"{symbols['prefix']}_OutputF32",
        f"{symbols['prefix']}_InputI32", f"{symbols['prefix']}_OutputI32",
        f"{symbols['prefix']}_InputI16", f"{symbols['prefix']}_OutputI16",
        f"{symbols['prefix']}_InputTrigger", f"{symbols['prefix']}_OutputTrigger",
        f"{symbols['prefix']}_Parameters",
        *(f"{symbols['prefix']}_{hook}" for hook in lifecycle),
    }
    if any(value in reserved_symbols or value.startswith("Orpheus") or value.startswith("ORPHEUS_")
           for key, value in symbols.items() if key != "prefix"):
        _fail("算法类型或函数与 ABI/生成壳保留符号冲突", "algorithm")
    interfaces = raw["interfaces"]
    if not isinstance(interfaces, dict) or not interfaces:
        _fail("interfaces 必须是非空映射", "interfaces")
    references = {}
    for key, item in interfaces.items():
        loc = f"interfaces.{key}"
        _identifier(key, loc)
        if key in {"algorithm", "buffer", "samples", "delivery"} or key.startswith("ORPHEUS_"):
            _fail("接口名称与生成字段冲突", loc)
        if not isinstance(item, dict):
            _fail("接口声明必须是映射", loc)
        kind = item.get("type")
        port = kind in {"audio", "control", "bulk", "debug", "trigger"}
        if kind not in {"audio", "control", "bulk", "debug", "trigger", "float", "int", "bool", "string"}:
            _fail("未知接口类型", loc + ".type")
        if set(item) - (_PORT if port else _PARAM):
            _fail("存在不适用于此接口类型的字段", loc)
        for field in ("process", "affects_signature", "readback", "persistent", "bindable", "control_source",
                      "readonly", "optional", "allow_unready"):
            if field in item and type(item[field]) is not bool:
                _fail("必须是布尔值", loc + "." + field)
        for field in ("name", "unit", "description", "file_ext", "bulk_slot", "layout"):
            if field in item:
                _string(item[field], loc + "." + field)
        if port:
            if item.get("direction") not in {"input", "output"}:
                _fail("数据端口必须声明 input/output 方向", loc + ".direction")
            if item.get("sample_format", "f32") not in {"f32", "i16", "i24", "i32"}:
                _fail("未知采样格式", loc + ".sample_format")
            if item.get("process") and item["direction"] != "input":
                _fail("process 只能用于输入端口", loc)
            if kind == "trigger":
                pcm_fields = {"sample_format", "channels", "sample_rate", "block_size", "layout",
                              "frame_size", "timing"}
                if pcm_fields.intersection(item):
                    _fail("纯 trigger 不允许 PCM 签名或布局字段", loc)
                if item["direction"] == "output" and "drive_port_no" not in item:
                    _fail("trigger 输出必须声明 drive_port_no", loc)
            if item.get("timing", "sync") not in {"sync", "latest"}:
                _fail("timing 必须是 sync/latest", loc)
            if item.get("timing") == "latest" and (kind == "audio" or item["direction"] != "input" or item.get("process")):
                _fail("latest 仅允许不驱动的非音频输入", loc)
            if "allow_unready" in item and (
                    kind != "bulk" or item["direction"] != "input"
                    or item.get("process") is True or item.get("timing") != "latest"):
                _fail("allow_unready 仅允许不驱动且 timing 为 latest 的 BULK 输入", loc + ".allow_unready")
            if kind == "audio" and ("layout" in item or "frame_size" in item):
                _fail("layout/frame_size 仅用于非音频端口", loc)
            for field in ("channels", "block_size", "sample_rate", "frame_size"):
                if field not in item:
                    continue
                value = item[field]
                if isinstance(value, str):
                    product = field in {"block_size", "channels"} and _SIGNATURE_PRODUCT.fullmatch(value)
                    rate_scale = field == "sample_rate" and _RATE_SCALE.fullmatch(value)
                    payload_ratio = field == "block_size" and "/" in value and _PAYLOAD_RATIO.fullmatch(value)
                    pattern = (r"param:[A-Za-z][A-Za-z0-9_]*" if field == "frame_size" else
                               r"(?:param:[A-Za-z][A-Za-z0-9_]*|[A-Za-z][A-Za-z0-9_]*:(?:channels|block_size|sample_rate))")
                    if not product and not rate_scale and not payload_ratio and not re.fullmatch(pattern, value):
                        _fail("签名表达式非法", loc + "." + field)
                    references[(key, field)] = value
                elif type(value) is not int or not (0 if field == "frame_size" else 1) <= value <= 4294967294:
                    _fail("必须是正整数或签名引用", loc + "." + field)
            if item.get("layout") == "halfcomplex" and (
                    kind != "bulk" or item.get("sample_format", "f32") != "f32"
                    or "block_size" not in item or "frame_size" not in item or item["frame_size"] == 0):
                _fail("halfcomplex 仅允许带显式 block_size 和正 frame_size 的 f32 BULK 端口", loc)
            if "drive_port_no" in item:
                value = item["drive_port_no"]
                if item["direction"] != "output" or type(value) is not int or not 1 <= value <= 4294967294:
                    _fail("驱动编号必须用于输出且在 1..UINT32_MAX-1", loc + ".drive_port_no")
            if "drive_divide" in item:
                value = item["drive_divide"]
                if "drive_port_no" not in item or type(value) is not int or not 1 <= value <= 4294967294:
                    _fail("drive_divide 必须是驱动出口上的正整数（仅显示用途）", loc)
        else:
            if "default" not in item and not (item.get("kind") == "probe" and item.get("access") == "read"):
                _fail("标量参数必须声明 default", loc)
            count = item.get("count", 1)
            if type(count) is not int or not 1 <= count <= 65536:
                _fail("参数 count 必须是 1..65536 的固定整数", loc)
            value = _parameter_default(item)
            is_array = is_array_parameter(item)
            if count > 1 and kind == "string":
                _fail("不支持固定字符串数组", loc + ".default")
            if is_array:
                encode_array_initializer(item, value, location=loc + ".default")
            else:
                valid = (isinstance(value, str) and "\0" not in value if kind == "string" else
                         _valid_numeric_element(kind, value))
                if not valid:
                    _fail("默认值类型或范围非法", loc + ".default")
            if kind == "string":
                _string(value, loc + ".default")
                capacity = item.get("capacity", 256)
                if type(capacity) is not int or not 1 <= capacity <= 262144:
                    _fail("字符串 capacity 必须是 1..262144 字节（包含 NUL）", loc + ".capacity")
                if len(value.encode("utf-8")) >= capacity:
                    _fail("默认字符串 UTF-8 字节数超过 capacity（需预留 NUL）", loc + ".default")
            elif "capacity" in item:
                _fail("capacity 只能用于 string 参数", loc)
            if item.get("update_policy", "block_boundary") not in _POLICIES:
                _fail("未知更新策略", loc + ".update_policy")
            if item.get("affects_signature") and item.get("update_policy") != "restart_required":
                _fail("影响签名的参数必须 restart_required", loc)
            if "range" in item and not is_array:
                bounds = item["range"]
                if (kind not in {"float", "int"} or not isinstance(bounds, list) or len(bounds) != 2
                        or any(type(v) not in ((int,) if kind == "int" else (int, float))
                               or not math.isfinite(v) for v in bounds)):
                    _fail("range 必须是同类型的有限数值二元数组", loc)
                limit = 2147483647 if kind == "int" else F32_MAX
                lower = -2147483648 if kind == "int" else -limit
                if not lower <= bounds[0] <= bounds[1] <= limit or not bounds[0] <= value <= bounds[1]:
                    _fail("默认值不在 range 中或 range 越界/倒置", loc)
            if is_array and item.get("affects_signature"):
                _fail("数组不能作为签名标量参数", loc)
            if "bulk" in item:
                bulk = item["bulk"]
                if not isinstance(bulk, dict) or set(bulk) - {"sample_format", "capacity", "storage", "double_bank", "range", "auto_register", "slot_key", "when"}:
                    _fail("bulk 只能声明资源键/格式/容量/存储/范围及双 bank、自动注册标志", loc + ".bulk")
                validate_configuration_conditions(bulk.get("when", {}), interfaces, loc + ".bulk.when", "BULK")
                if "slot_key" in bulk and (not isinstance(bulk["slot_key"], str) or not re.fullmatch(
                        r"[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)*", bulk["slot_key"])):
                    _fail("slot_key 必须是由字母开头标识符组成的点分资源键", loc + ".bulk.slot_key")
                for flag in ("double_bank", "auto_register"):
                    if flag in bulk and type(bulk[flag]) is not bool:
                        _fail(f"{flag} 必须是布尔值", loc + ".bulk")
                if is_array:
                    if "storage" in bulk or "capacity" in bulk:
                        _fail("自动数组的存储与容量由接口 count 决定，不能重复指定", loc + ".bulk")
                    expected = {"float": "f32", "int": "i32", "bool": "bool"}[kind]
                    if bulk.get("sample_format", expected) != expected:
                        _fail("BULK 格式必须与数组元素类型相同", loc + ".bulk")
                else:
                    if kind != "string":
                        _fail("私有 BULK 绑定仅用于 string 初始化参数或固定数值数组", loc + ".bulk")
                    if bulk.get("sample_format") not in {"f32", "i32", "bool"}:
                        _fail("BULK sample_format 仅支持 f32/i32/bool", loc + ".bulk")
                    capacity = bulk.get("capacity")
                    if type(capacity) is not int or not 1 <= capacity <= 65536:
                        _fail("BULK capacity 必须是 1..65536 的物理元素容量", loc + ".bulk")
                    storage = bulk.get("storage")
                    if not isinstance(storage, str) or not re.fullmatch(r"algorithm(?:\.[A-Za-z][A-Za-z0-9_]*)+", storage):
                        _fail("storage 必须是 algorithm.<字段链>，不能是指针/下标/C 表达式", loc + ".bulk.storage")
                    for field in storage.split("."):
                        _identifier(field, loc + ".bulk.storage")
                if "range" in bulk:
                    if is_array:
                        _fail("自动数组范围仅由外层 range 声明，bulk 内不能重复指定", loc + ".bulk.range")
                    resource_kind = {"f32": "float", "i32": "int", "bool": "bool"}.get(bulk.get("sample_format"), kind)
                    bounds = bulk["range"]
                    if (resource_kind == "bool" or not isinstance(bounds, list) or len(bounds) != 2 or
                            any(type(v) not in ((int,) if resource_kind == "int" else (int, float))
                                or not math.isfinite(v) for v in bounds) or bounds[0] > bounds[1]):
                        _fail("BULK range 必须是同类型的有序有限数值区间", loc + ".bulk.range")
                    lower, upper = (-2147483648, 2147483647) if resource_kind == "int" else (-F32_MAX, F32_MAX)
                    if not lower <= bounds[0] <= bounds[1] <= upper:
                        _fail("BULK range 超出元素类型范围", loc + ".bulk.range")
            if is_array or "bulk" in item:
                if item.get("kind", "bulk") not in ({"bulk", "probe", "state"} if is_array else {"bulk"}):
                    _fail("资源接口 kind 必须是 bulk（数组可为 probe/state）", loc)
                if "bulk_slot" in item and item["bulk_slot"] != key:
                    _fail("资源必须与本接口同名，不能另写 bulk_slot 映射", loc)
            if item.get("kind", "setting") not in {"setting", "command", "bulk", "probe", "state"}:
                _fail("未知参数资源类别", loc + ".kind")
            if item.get("access", "read_write") not in {"read", "write", "read_write"}:
                _fail("未知访问权限", loc + ".access")
            if item.get("widget", "number") not in {"number", "text", "slider", "select", "checkbox", "file", "placeholder_ports"}:
                _fail("未知参数控件", loc + ".widget")
            if item.get("options_source", "devices") != "devices":
                _fail("未知选项来源", loc + ".options_source")
            if "options" in item:
                if not isinstance(item["options"], list):
                    _fail("options 必须是数组", loc)
                for option in item["options"]:
                    values = [option]
                    if isinstance(option, dict):
                        if set(option) != {"value", "label"}:
                            _fail("选项映射必须是 value/label", loc)
                        _string(option["label"], loc + ".options.label")
                        values = [option["value"]]
                    if any(type(v) not in (str, int, float, bool) or
                           (isinstance(v, float) and not math.isfinite(v)) for v in values):
                        _fail("选项值必须是有限标量", loc)
                    for v in values:
                        if isinstance(v, str):
                            _string(v, loc + ".options")
            if item.get("bindable") and (item.get("affects_signature") or
                    item.get("update_policy", "block_boundary") not in {"immediate", "block_boundary", "smoothed"}
                    or item.get("readonly") or item.get("access") == "read"
                    or item.get("kind") in {"probe", "state"}):
                _fail("bindable 参数不能影响签名且须支持运行期更新", loc)
            if item.get("control_source") and (item.get("access") == "write" or
                    not item.get("readback", True) and item.get("kind") not in {"probe", "state"}):
                _fail("control_source 参数必须可读", loc)
            if "shape" in item:
                if not isinstance(item["shape"], list):
                    _fail("shape 必须是维度数组", loc)
                for dimension in item["shape"]:
                    if type(dimension) is int and dimension > 0:
                        continue
                    if dimension == "config:frames" and (is_array_parameter(item) or "bulk" in item):
                        continue
                    if not isinstance(dimension, str) or not re.fullmatch(r"param:[A-Za-z][A-Za-z0-9_]*", dimension):
                        _fail("shape 维度必须是正整数、param 引用或资源的 config:frames", loc)
                    references[(key, f"shape{len(references)}")] = dimension
    _validate_port_groups(interfaces)
    _validate_input_alternatives(raw.get("input_alternatives", []), interfaces)
    count_parameters = {item["count"].split(":")[1] for item in interfaces.values()
                        if "direction" in item and isinstance(item.get("count"), str)}
    visiting, done = set(), set()
    product_bounds = {}
    automatic_rates = set()

    def check_ref(node):
        if node in visiting:
            _fail("签名引用存在循环", f"interfaces.{node[0]}.{node[1]}")
        if node in done or node not in references:
            return product_bounds.get(node)
        visiting.add(node)
        maximum = 1
        inherited_product = None
        rate_scale = node[1] == "sample_rate" and _RATE_SCALE.fullmatch(references[node])
        tokens = re.split(r"([*/])", references[node])
        atoms = tokens[::2]
        limit = 65536 if node[1] == "channels" and len(atoms) > 1 else 4294967295
        for index, atom in enumerate(atoms):
            divisor = index > 0 and tokens[index * 2 - 1] == "/"
            if atom.isdecimal():
                if len(atom) > 10 or (len(atom) == 10 and atom > "4294967295"):
                    _fail("签名乘数超出 uint32 上限", f"interfaces.{node[0]}.{node[1]}")
                if not divisor:
                    maximum *= int(atom)
                if maximum > limit:
                    _fail(f"签名乘积在声明参数范围内超出上限 {limit}", f"interfaces.{node[0]}.{node[1]}")
                continue
            target, field = atom.split(":")
            if target == "task":
                if field not in {"block_size", "sample_rate"}:
                    _fail("Task 仅支持 task:block_size/task:sample_rate 引用", f"interfaces.{node[0]}.{node[1]}")
            elif target == "param":
                parameter = interfaces.get(field, {})
                if (parameter.get("type") != "int" or is_array_parameter(parameter) or
                        not (parameter.get("affects_signature") or field in count_parameters)):
                    _fail("签名参数引用必须指向 affects_signature 的 int 参数", f"interfaces.{node[0]}.{node[1]}")
                automatic_rate = (not rate_scale and node[1] == "sample_rate" and "range" in parameter
                                  and parameter["range"][0] == 0)
                minimum = 0 if automatic_rate else 1
                if (_parameter_default(parameter) < minimum or
                        ("range" in parameter and parameter["range"][0] < minimum)):
                    _fail("签名参数必须保持正数", f"interfaces.{target}.{field}")
                if automatic_rate:
                    automatic_rates.add(node)
                if not divisor:
                    maximum *= parameter.get("range", [1, 2147483647])[1]
            else:
                other = interfaces.get(target, {})
                if other.get("direction") not in {"input", "output"} or other.get("type") == "trigger":
                    _fail("签名引用不存在", f"interfaces.{node[0]}.{node[1]}")
                inherited_product = check_ref((target, field))
                if (target, field) in automatic_rates:
                    if node[1] != "sample_rate":
                        _fail("可跟随输入的采样率不能用作载荷尺寸", f"interfaces.{node[0]}.{node[1]}")
                    automatic_rates.add(node)
            if len(atoms) > 1 and maximum > limit:
                _fail(f"签名乘积在声明参数范围内超出上限 {limit}", f"interfaces.{node[0]}.{node[1]}")
        visiting.remove(node)
        done.add(node)
        product_bound = maximum if len(atoms) > 1 else inherited_product
        if node[1] == "channels" and product_bound is not None and product_bound > 65536:
            _fail("通道乘积在声明参数范围内超出上限 65536", f"interfaces.{node[0]}.{node[1]}")
        product_bounds[node] = product_bound
        return product_bound

    for node in references:
        check_ref(node)

    def default_signature(key, field):
        value = interfaces[key].get(field)
        if not isinstance(value, str):
            return value
        result = 1
        tokens = re.split(r"([*/])", value)
        for index, atom in enumerate(tokens[::2]):
            if atom.isdecimal():
                factor = int(atom)
            else:
                target, attr = atom.split(":")
                factor = (_parameter_default(interfaces[attr]) if target == "param" else
                          None if target == "task" else default_signature(target, attr))
            if factor is None:
                return None
            result = Fraction(result, factor) if index > 0 and tokens[index * 2 - 1] == "/" else result * factor
        return result

    for key, item in interfaces.items():
        if "direction" in item and isinstance(item.get("block_size"), str) and "/" in item["block_size"]:
            payload = default_signature(key, "block_size")
            if payload is not None and (payload <= 0 or Fraction(payload).denominator != 1):
                _fail("默认载荷长度必须为正整数，比例不得截断", f"interfaces.{key}.block_size")
        if item.get("layout") == "halfcomplex" or isinstance(item.get("frame_size"), str):
            size = default_signature(key, "frame_size")
            payload = default_signature(key, "block_size")
            if size is not None and payload is not None and payload % size:
                _fail("frame_size 必须整除默认载荷长度", f"interfaces.{key}.frame_size")
    slot_keys = set()
    for key, item in interfaces.items():
        if "direction" in item:
            continue
        slot_key = item.get("bulk", {}).get("slot_key", key)
        if slot_key in slot_keys:
            _fail("资源键与其他参数或资源注册键重复", f"interfaces.{key}.bulk.slot_key")
        slot_keys.add(slot_key)
    storages = []
    for resource in component_bulk_resources(interfaces):
        size = 1
        for dimension in resource["shape"]:
            value = (1 if dimension == "config:frames" else
                     _parameter_default(interfaces[dimension.split(":")[1]]) if isinstance(dimension, str) else dimension)
            size *= value
        active = configuration_conditions_match(resource.get("when", {}), interfaces,
            {key: _parameter_default(interfaces[key]) for key in resource.get("when", {})})
        if size < 1 or active and size > resource["count"]:
            _fail("资源默认 shape 的元素数必须在物理容量内", f"interfaces.{resource['id']}.shape")
        if (is_array_parameter(interfaces[resource["id"]]) and
                not any(isinstance(dimension, str) for dimension in resource["shape"]) and size != resource["count"]):
            _fail("固定数组的常量 shape 必须与 count 完全相等", f"interfaces.{resource['id']}.shape")
        if any(resource["storage"] == other or resource["storage"].startswith(other + ".") or
               other.startswith(resource["storage"] + ".") for other in storages):
            _fail("不同资源不能绑定同一存储字段或父子重叠字段", f"interfaces.{resource['id']}.bulk.storage")
        storages.append(resource["storage"])
        if f"resource_count_{resource['id']}" in symbols.values():
            _fail("算法符号与生成的资源计数函数冲突", "algorithm")
        if f"{symbols['prefix']}_register_{resource['id']}" in symbols.values():
            _fail("算法符号与生成的资源注册 helper 冲突", "algorithm")
        if f"register_{resource['id']}" in LIFECYCLE_HOOKS:
            _fail("资源名与生命周期注册 hook 冲突", f"interfaces.{resource['id']}")
    for key, value in raw.get("scheduling", {}).items():
        if isinstance(value, str):
            for name in re.findall(r"param:([A-Za-z][A-Za-z0-9_]*)", value):
                parameter = interfaces.get(name, {})
                if parameter.get("type") != "int" or is_array_parameter(parameter) or parameter.get("default", 0) < 1:
                    _fail("调度引用必须指向正整数标量参数", f"scheduling.{key}")
    validate_host_bindings(
        [{"id": key, **item} for key, item in interfaces.items() if "direction" in item],
        [{"id": key, **item} for key, item in interfaces.items() if "direction" not in item],
        component_bulk_resources(interfaces), location="interfaces")


def load_component_definition(path: Path) -> dict:
    """加载唯一落盘配置；不创建、修改任何文件，返回已校验的新声明。"""
    try:
        raw = yaml.load(Path(path).read_text(encoding="utf-8"), Loader=_UniqueLoader)
    except (OSError, UnicodeError, yaml.YAMLError, RecursionError) as exc:
        raise ComponentDefinitionError(f"无法读取 YAML：{exc}", str(path), "read_error") from exc
    try:
        _validate(raw)
    except (TypeError, RecursionError, OverflowError) as exc:
        raise ComponentDefinitionError("声明含非法类型、递归别名或过大数值") from exc
    return raw


def normalize_component_definition(raw: dict) -> dict:
    """深拷贝内部视图；bulk_slots.id 是外部键，bulk_bindings.id 是 C 接口名。

    端口组保留 count 并派生 count_min/count_max/count_default/count_param；
    被 count 引用的参数在内部 parameters 中自动标记 affects_signature。
    """
    try:
        _validate(raw)
    except (TypeError, RecursionError, OverflowError) as exc:
        raise ComponentDefinitionError("声明含非法类型、递归别名或过大数值") from exc
    result = copy.deepcopy(raw)
    result["ports"] = []
    result["parameters"] = []
    count_parameters = {item["count"].split(":")[1] for item in raw["interfaces"].values()
                        if "direction" in item and isinstance(item.get("count"), str)}
    for key, value in raw["interfaces"].items():
        item = {"id": key, **copy.deepcopy(value)}
        if "direction" in value:
            if value["type"] != "trigger":
                item.setdefault("sample_format", "f32")
                item.setdefault("channels", 1)
            item["process"] = value.get("process", False)
            item["propagate_drive"] = (
                "drive_port_no" in value if value["direction"] == "output" else item["process"]
            )
            if "count" in value:
                item["count_min"], item["count_max"], item["count_default"] = _port_count_bounds(
                    key, value, raw["interfaces"])
                if isinstance(value["count"], str):
                    item["count_param"] = value["count"].split(":")[1]
            result["ports"].append(item)
        else:
            if "default" not in item:
                item["default"] = _parameter_default(item)
            item.setdefault("update_policy", "block_boundary")
            if key in count_parameters:
                item["affects_signature"] = True
            if value["type"] == "string":
                item.setdefault("capacity", 256)
            if is_array_parameter(value) or "bulk" in value:
                item.setdefault("kind", "bulk")
                item["bulk_slot"] = value.get("bulk", {}).get("slot_key", key)
            result["parameters"].append(item)
    symbols = component_symbols(raw)
    result.update({"sources": ["generated/component_shell.c", "user/algorithm.c"],
                   "headers": ["generated/component_api.h", "generated/component_state.h", "user/algorithm.h"],
                   "include_dirs": [".", "generated", "user"], "state_type": symbols["state_type"],
                   "algorithm_symbols": symbols, "abi_version": 7, "package_type": "source"})
    if (raw.get("execution") or {}).get("none"):
        # 声明式平台节点（execution.none）：不生成也不编译组件壳，清单不指向生成物
        result.update({"sources": [], "headers": [], "include_dirs": [],
                       "state_type": None, "algorithm_symbols": None})
    resources = component_bulk_resources(raw["interfaces"])
    if resources:
        result["bulk_slots"] = [
            {**{k: v for k, v in resource.items() if k not in {"storage", "auto_register", "slot_key"}},
             "id": resource["slot_key"]}
            for resource in resources]
        result["bulk_bindings"] = resources
    return result


def cli_main(generate=False, argv=None) -> int:
    """独立脚本共用诊断协议；0 成功，1 声明/生成失败，2 命令行用法错误。"""
    import argparse
    import sys
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="独立组件生成" if generate else "只读校验组件声明")
    parser.add_argument("path", type=Path)
    parser.add_argument("--format", choices=("text", "json"), default="text")
    args = parser.parse_args(argv)
    try:
        if generate:
            from .component_codegen import generate_component
            files = [str(p) for p in generate_component(args.path)]
        else:
            load_component_definition(args.path)
            files = []
        result = {"ok": True, "diagnostics": [], "files": files}
    except (ComponentDefinitionError, OSError) as exc:
        diagnostic = exc.diagnostic if isinstance(exc, ComponentDefinitionError) else {
            "code": "io_error", "location": str(args.path), "message": f"文件操作失败：{exc}"}
        result = {"ok": False, "diagnostics": [diagnostic], "files": []}
    if args.format == "json":
        print(json.dumps(result, ensure_ascii=False))
    else:
        print("生成完成" if result["ok"] and generate else "校验通过" if result["ok"] else
              "\n".join(f"{d['location']}: {d['message']}" for d in result["diagnostics"]))
    return 0 if result["ok"] else 1
