"""独立组件声明的只读加载、校验和内部视图派生。"""

from __future__ import annotations

import copy
import json
import math
import re
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
_TOP = {"id", "name", "version", "abi_version", "package_type", "clock_source", "clock_domain",
        "category", "description", "interfaces", "algorithm"}
_PORT = {"name", "direction", "type", "sample_format", "channels", "block_size", "sample_rate",
         "process", "drive_port_no", "drive_divide", "count"}
_PARAM = {"name", "type", "default", "range", "update_policy", "affects_signature", "readback",
          "persistent", "unit", "count", "shape"}


def _fail(message, location="$", code="invalid_definition"):
    raise ComponentDefinitionError(message, location, code)


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
    result.update(raw.get("algorithm", {}))
    return result


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
        if not isinstance(raw[key], str) or not raw[key] or "\0" in raw[key]:
            _fail("必须是非空字符串", key)
    if "abi_version" in raw and (type(raw["abi_version"]) is not int or raw["abi_version"] != 7):
        _fail("此工具仅支持 ABI v7", "abi_version")
    if raw.get("package_type", "source") != "source":
        _fail("此工具仅支持 source 组件包", "package_type")
    if "clock_source" in raw and type(raw["clock_source"]) is not bool:
        _fail("必须是布尔值", "clock_source")
    algorithm = raw.get("algorithm", {})
    if not isinstance(algorithm, dict) or set(algorithm) - {"prefix", "state_type", "io_type", "private_type", "function"}:
        _fail("算法配置只允许 prefix/state_type/io_type/private_type/function", "algorithm")
    for key, value in component_symbols(raw).items():
        _identifier(value, f"algorithm.{key}")
    symbols = component_symbols(raw)
    if len(set(symbols.values())) != len(symbols):
        _fail("算法类型和符号不能重名", "algorithm")
    reserved_symbols = {
        "create", "destroy", "prepare", "reset", "process", "set_parameter", "get_parameter",
        "register_slots", "get_descriptor", "valid_buffer", "interface", "descriptor", "ports", "params",
        "orpheus_get_interface", "orpheus_emit", "orpheus_process_next", "orpheus_process_branch",
        f"{symbols['prefix']}_InputF32", f"{symbols['prefix']}_OutputF32",
    }
    if any(value in reserved_symbols or value.startswith("Orpheus") or value.startswith("ORPHEUS_")
           for key, value in symbols.items() if key != "prefix"):
        _fail("算法类型或函数与 ABI/生成壳保留符号冲突", "algorithm")
    interfaces = raw["interfaces"]
    if not isinstance(interfaces, dict) or not interfaces:
        _fail("interfaces 必须是非空映射", "interfaces")
    drives = set()
    references = {}
    for key, item in interfaces.items():
        loc = f"interfaces.{key}"
        _identifier(key, loc)
        if key in {"algorithm", "buffer", "samples", "delivery"} or key.startswith("ORPHEUS_"):
            _fail("接口名称与生成字段冲突", loc)
        if not isinstance(item, dict):
            _fail("接口声明必须是映射", loc)
        kind = item.get("type")
        port = kind in {"audio", "control", "bulk", "debug"}
        if kind not in {"audio", "control", "bulk", "debug", "float", "int", "bool", "string"}:
            _fail("未知接口类型", loc + ".type")
        if set(item) - (_PORT if port else _PARAM):
            _fail("存在不适用于此接口类型的字段", loc)
        for field in ("process", "affects_signature", "readback", "persistent"):
            if field in item and type(item[field]) is not bool:
                _fail("必须是布尔值", loc + "." + field)
        for field in ("name", "unit"):
            if field in item and (not isinstance(item[field], str) or "\0" in item[field]):
                _fail("必须是字符串且不能含 NUL", loc + "." + field)
        if port:
            if item.get("direction") not in {"input", "output"}:
                _fail("数据端口必须声明 input/output 方向", loc + ".direction")
            if item.get("sample_format", "f32") not in {"f32", "i16", "i24", "i32"}:
                _fail("未知采样格式", loc + ".sample_format")
            if item.get("process") and item["direction"] != "input":
                _fail("process 只能用于输入端口", loc)
            for field in ("channels", "block_size", "sample_rate", "count"):
                if field not in item:
                    continue
                value = item[field]
                if isinstance(value, str):
                    if not re.fullmatch(r"(?:param:[A-Za-z][A-Za-z0-9_]*|[A-Za-z][A-Za-z0-9_]*:(?:channels|block_size|sample_rate))", value):
                        _fail("签名表达式非法", loc + "." + field)
                    references[(key, field)] = value
                elif type(value) is not int or not 1 <= value <= 4294967294:
                    _fail("必须是正整数或签名引用", loc + "." + field)
            if "drive_port_no" in item:
                value = item["drive_port_no"]
                if item["direction"] != "output" or type(value) is not int or not 1 <= value <= 4294967294:
                    _fail("驱动编号必须用于输出且在 1..UINT32_MAX-1", loc + ".drive_port_no")
                if value in drives:
                    _fail("驱动编号重复", loc + ".drive_port_no")
                drives.add(value)
            if "drive_divide" in item:
                value = item["drive_divide"]
                if "drive_port_no" not in item or type(value) is not int or not 1 <= value <= 4294967294:
                    _fail("drive_divide 必须是驱动出口上的正整数（仅显示用途）", loc)
        else:
            if "default" not in item:
                _fail("标量参数必须声明 default", loc)
            value = item["default"]
            valid = ((kind == "float" and type(value) in (float, int) and math.isfinite(value) and abs(value) <= 3.402823466e38)
                     or (kind == "int" and type(value) is int and -2147483648 <= value <= 2147483647)
                     or (kind == "bool" and type(value) is bool)
                     or (kind == "string" and isinstance(value, str) and "\0" not in value))
            if not valid:
                _fail("默认值类型或范围非法", loc + ".default")
            if item.get("update_policy", "block_boundary") not in _POLICIES:
                _fail("未知更新策略", loc + ".update_policy")
            if item.get("affects_signature") and item.get("update_policy") != "restart_required":
                _fail("影响签名的参数必须 restart_required", loc)
            if "range" in item:
                bounds = item["range"]
                if (kind not in {"float", "int"} or not isinstance(bounds, list) or len(bounds) != 2
                        or any(type(v) not in ((int,) if kind == "int" else (int, float))
                               or not math.isfinite(v) for v in bounds)):
                    _fail("range 必须是同类型的有限数值二元数组", loc)
                limit = 2147483647 if kind == "int" else 3.402823466e38
                lower = -2147483648 if kind == "int" else -limit
                if not lower <= bounds[0] <= value <= bounds[1] <= limit:
                    _fail("默认值不在 range 中或 range 越界/倒置", loc)
            if "count" in item and (type(item["count"]) is not int or item["count"] < 1):
                _fail("参数 count 必须是正整数", loc)
            if "shape" in item:
                _fail("首版不支持参数 shape", loc, "unsupported_feature")
    visiting, done = set(), set()

    def check_ref(node):
        if node in visiting:
            _fail("签名引用存在循环", f"interfaces.{node[0]}.{node[1]}")
        if node in done or node not in references:
            return
        visiting.add(node)
        target, field = references[node].split(":")
        if target == "param":
            parameter = interfaces.get(field, {})
            if parameter.get("type") != "int" or not parameter.get("affects_signature"):
                _fail("签名参数引用必须指向 affects_signature 的 int 参数", f"interfaces.{node[0]}.{node[1]}")
            if parameter["default"] < 1 or ("range" in parameter and parameter["range"][0] < 1):
                _fail("签名参数必须保持正数", f"interfaces.{target}.{field}")
        else:
            other = interfaces.get(target, {})
            if other.get("direction") not in {"input", "output"} or field not in other:
                _fail("签名引用不存在", f"interfaces.{node[0]}.{node[1]}")
            check_ref((target, field))
        visiting.remove(node)
        done.add(node)

    for node in references:
        check_ref(node)


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
    """深拷贝新声明，派生 Registry/构建共用的 ports、parameters 和源码元数据。"""
    try:
        _validate(raw)
    except (TypeError, RecursionError, OverflowError) as exc:
        raise ComponentDefinitionError("声明含非法类型、递归别名或过大数值") from exc
    result = copy.deepcopy(raw)
    result["ports"] = []
    result["parameters"] = []
    for key, value in raw["interfaces"].items():
        item = {"id": key, **copy.deepcopy(value)}
        if "direction" in value:
            item.setdefault("sample_format", "f32")
            item.setdefault("channels", 1)
            item["process"] = value.get("process", False)
            item["propagate_drive"] = (
                "drive_port_no" in value if value["direction"] == "output" else item["process"]
            )
            result["ports"].append(item)
        else:
            item.setdefault("update_policy", "block_boundary")
            result["parameters"].append(item)
    symbols = component_symbols(raw)
    result.update({"sources": ["generated/component_shell.c", "user/algorithm.c"],
                   "headers": ["generated/component_api.h", "generated/component_state.h", "user/algorithm.h"],
                   "include_dirs": [".", "generated", "user"], "state_type": symbols["state_type"],
                   "algorithm_symbols": symbols, "abi_version": 7, "package_type": "source"})
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
