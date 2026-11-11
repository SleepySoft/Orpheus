"""独立组件工具的声明、文件事务、CLI 和真实 ABI 调用验证。"""

import copy
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import uuid
from pathlib import Path

import pytest
import yaml

from orpheus_core.component_definition import (
    ComponentDefinitionError,
    F32_MAX,
    encode_array_initializer,
    is_array_parameter,
    load_component_definition,
    normalize_component_definition,
)
from orpheus_core.component_codegen import (
    assert_component_generated_current,
    check_generated,
    generate_component,
)

ROOT = Path(__file__).resolve().parents[2]
PYTHON = ROOT / ".venv" / "Scripts" / "python.exe"


@pytest.fixture
def package():
    # 测试产物保留在仓库内，避免依赖系统临时目录。
    root = ROOT / "build" / f"组件 壳测试_{uuid.uuid4().hex}"
    root.mkdir(parents=True)
    yield root
    shutil.rmtree(root)


@pytest.fixture
def raw():
    return {
        "id": "test.pilot", "name": "测试组件", "version": "1.0.0", "abi_version": 7,
        "interfaces": {
            "in": {"direction": "input", "type": "audio", "sample_format": "f32",
                   "channels": 2, "process": True},
            "out": {"direction": "output", "type": "audio", "channels": 2,
                    "drive_port_no": 1, "drive_divide": 2},
            "gain": {"type": "float", "default": 1, "range": [0, 4], "update_policy": "block_boundary"},
            "channels": {"type": "int", "default": 2, "range": [1, 8],
                         "update_policy": "restart_required", "affects_signature": True},
            "enabled": {"type": "bool", "default": True},
        },
    }


def save(package, raw):
    path = package / "component.yaml"
    path.write_text(yaml.safe_dump(raw, allow_unicode=True, sort_keys=False), encoding="utf-8", newline="\n")
    return path


def snapshot(package):
    return {p.relative_to(package).as_posix(): p.read_bytes() for p in package.rglob("*") if p.is_file()}


def test_normalized_is_deep_copy_and_preserves_order(package, raw):
    raw["interfaces"]["in"]["block_size"] = 32
    raw["interfaces"]["out"]["block_size"] = "in:block_size"
    path = save(package, raw)
    before = snapshot(package)
    loaded = load_component_definition(path)
    view = normalize_component_definition(loaded)
    assert snapshot(package) == before
    assert "ports" not in loaded
    assert [p["id"] for p in view["ports"]] == ["in", "out"]
    assert [p["id"] for p in view["parameters"]] == ["gain", "channels", "enabled"]
    assert view["ports"][0]["process"] is True
    assert view["ports"][1]["drive_port_no"] == 1
    assert view["ports"][1]["block_size"] == "in:block_size"
    assert view["state_type"] == "test_pilot_State"
    assert view["include_dirs"] == [".", "generated", "user"]
    assert "generated/component_state.h" in view["headers"]
    view["interfaces"]["gain"]["default"] = 99
    assert loaded["interfaces"]["gain"]["default"] == 1


@pytest.mark.parametrize("process", [None, False, True])
@pytest.mark.parametrize("drive_port_no", [None, 1])
def test_normalized_audio_drive_requires_explicit_declaration(package, raw, process, drive_port_no):
    input_port, output_port = raw["interfaces"]["in"], raw["interfaces"]["out"]
    if process is None:
        del input_port["process"]
    else:
        input_port["process"] = process
    if drive_port_no is None:
        del output_port["drive_port_no"], output_port["drive_divide"]
    else:
        output_port["drive_port_no"] = drive_port_no
    path = save(package, raw)
    before = snapshot(package)
    loaded = load_component_definition(path)
    original = copy.deepcopy(loaded)
    normalized = normalize_component_definition(loaded)
    ports = {port["id"]: port for port in normalized["ports"]}
    assert ports["in"]["process"] is (process is True)
    assert ports["in"]["propagate_drive"] is (process is True)
    assert ports["out"]["propagate_drive"] is (drive_port_no is not None)
    assert normalized["interfaces"] == original["interfaces"]
    assert loaded == original
    assert all("propagate_drive" not in declaration for declaration in loaded["interfaces"].values())
    assert snapshot(package) == before


@pytest.mark.parametrize("shape", ["no_outputs", "undriven_output"])
def test_generated_files_end_with_exactly_one_lf(package, raw, shape):
    if shape == "no_outputs":
        del raw["interfaces"]["out"]
    else:
        del raw["interfaces"]["out"]["drive_port_no"], raw["interfaces"]["out"]["drive_divide"]
    paths = generate_component(save(package, raw))
    for path in paths:
        content = path.read_bytes()
        assert content.endswith(b"\n"), path.name
        assert not content.endswith(b"\n\n"), path.name


def test_algorithm_symbol_overrides(raw):
    raw["algorithm"] = {"prefix": "pilot", "private_type": "PilotHistory"}
    symbols = normalize_component_definition(raw)["algorithm_symbols"]
    assert symbols == {"prefix": "pilot", "state_type": "pilot_State", "io_type": "pilot_IO",
                       "private_type": "PilotHistory", "function": "pilot_algorithm"}
    for symbol in ("create", "OrpheusBuffer", "pilot_InputF32"):
        raw["algorithm"]["state_type"] = symbol
        with pytest.raises(ComponentDefinitionError):
            normalize_component_definition(raw)


@pytest.mark.parametrize("target,key,value", [
    ("out", "drive_port_no", 0), ("out", "drive_port_no", 4294967295),
    ("out", "drive_port_no", True), ("out", "drive_port_no", 1.5),
    ("out", "drive_port_no", "1"), ("out", "drive_divide", False),
    ("in", "process", 1), ("in", "channels", False),
    ("in", "channels", "param:missing"), ("in", "channels", "param:gain"),
    ("out", "block_size", "missing:block_size"), ("out", "block_size", "out:block_size"),
    ("gain", "default", True), ("gain", "default", float("nan")),
    ("gain", "default", 1e100), ("gain", "range", [4, 1]),
    ("gain", "default", "1"), ("gain", "range", [True, 4]),
    ("channels", "default", 2.5), ("enabled", "default", 1),
    ("channels", "update_policy", "immediate"),
    ("gain", "garbage", 1), ("out", "direction", "sideways"),
    ("in", "sample_format", "bad"), ("out", "process", True),
])
def test_invalid_declarations_are_readonly(package, raw, target, key, value):
    raw["interfaces"][target][key] = value
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        load_component_definition(path)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


def test_duplicate_keys_and_illegal_top_level(package, raw):
    path = save(package, raw)
    path.write_bytes(path.read_bytes() + b"name: duplicate\n")
    with pytest.raises(ComponentDefinitionError, match="重复键"):
        load_component_definition(path)
    for key in ("ports", "parameters"):
        candidate = copy.deepcopy(raw)
        candidate[key] = []
        with pytest.raises(ComponentDefinitionError):
            normalize_component_definition(candidate)
    raw["interfaces"]["other"] = dict(raw["interfaces"]["out"])
    with pytest.raises(ComponentDefinitionError, match="驱动编号预留区间重复"):
        normalize_component_definition(raw)


def test_deterministic_generation_and_user_preservation(package, raw):
    path = save(package, raw)
    files = generate_component(path)
    assert len(files) == 8
    assert_component_generated_current(path)
    check_generated(path)
    assert not list(package.glob(".component_codegen*"))
    first = snapshot(package)
    assert len(generate_component(path)) == 5
    assert snapshot(package) == first
    for name in ("user/algorithm.c", "user/algorithm.h", "CMakeLists.txt"):
        (package / name).write_bytes(b"/* handwritten */\n")
    users = snapshot(package)
    raw["interfaces"]["out"]["drive_port_no"] = 4294967294
    save(package, raw)
    with pytest.raises(ComponentDefinitionError, match="配置已变化"):
        assert_component_generated_current(path)
    generate_component(path)
    for name in ("user/algorithm.c", "user/algorithm.h", "CMakeLists.txt"):
        assert (package / name).read_bytes() == users[name]
    assert "UINT32_C(4294967294)" in (package / "generated" / "component_api.h").read_text(encoding="utf-8")
    assert_component_generated_current(path)


@pytest.mark.parametrize("mode", ["no_manifest", "edited", "foreign", "bad_manifest"])
def test_generated_ownership(package, raw, mode):
    path = save(package, raw)
    if mode == "no_manifest":
        (package / "generated").mkdir()
        (package / "generated" / "component_api.h").write_bytes(b"handmade\n")
    else:
        generate_component(path)
        if mode == "edited":
            (package / "generated" / "component_api.h").write_bytes(b"handmade\n")
        elif mode == "foreign":
            (package / "generated" / "unrelated.txt").write_bytes(b"handmade\n")
        else:
            (package / "generated" / "component_codegen.json").write_bytes(b"{}")
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("feature", ["format", "array", "execution"])
def test_unsupported_generation_is_explicit_and_readonly(package, raw, feature):
    if feature == "format":
        raw["interfaces"]["in"]["sample_format"] = "i24"
    elif feature == "array":
        raw["interfaces"]["gain"]["shape"] = [2]
    else:
        raw["execution"] = {"none": True}
    path = save(package, raw)
    load_component_definition(path)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError) as error:
        generate_component(path)
    assert error.value.diagnostic["code"] == "unsupported_feature"
    assert snapshot(package) == before


def test_independent_cli_chinese_path_other_cwd(package, raw):
    path = save(package, raw)
    for script in ("validate_component.py", "generate_component.py"):
        result = subprocess.run([str(PYTHON), str(ROOT / "scripts" / script), str(path), "--format", "json"],
                                cwd=package, capture_output=True, encoding="utf-8")
        assert result.returncode == 0, result.stderr
        assert json.loads(result.stdout)["ok"] is True
    path.write_bytes(path.read_bytes() + b"id: duplicate\n")
    before = snapshot(package)
    for output in ("json", "text"):
        result = subprocess.run([str(PYTHON), str(ROOT / "scripts" / "generate_component.py"),
                                 str(path), "--format", output], cwd=package,
                                capture_output=True, encoding="utf-8")
        assert result.returncode == 1
        if output == "json":
            assert json.loads(result.stdout)["diagnostics"][0]["code"] == "duplicate_key"
        else:
            assert "重复键" in result.stdout
    assert snapshot(package) == before


def test_rollback_on_write_failure(package, raw, monkeypatch):
    path = save(package, raw)
    generate_component(path)
    raw["interfaces"]["gain"]["default"] = 2
    save(package, raw)
    before = snapshot(package)
    original = Path.rename

    def fail_stage(self, target):
        if self.name.startswith(".component_codegen_stage_"):
            raise OSError("模拟替换失败")
        return original(self, target)

    monkeypatch.setattr(Path, "rename", fail_stage)
    with pytest.raises(OSError):
        generate_component(path)
    assert snapshot(package) == before
    assert not list(package.glob(".component_codegen*"))


def test_first_generation_failure_rolls_back_user_files(package, raw, monkeypatch):
    path = save(package, raw)
    before = snapshot(package)
    original = Path.rename

    def fail_stage(self, target):
        if self.name.startswith(".component_codegen_stage_"):
            raise OSError("模拟首次替换失败")
        return original(self, target)

    monkeypatch.setattr(Path, "rename", fail_stage)
    with pytest.raises(OSError):
        generate_component(path)
    assert snapshot(package) == before
    assert sorted(p.name for p in package.iterdir()) == ["component.yaml"]


def test_visual_drive_divide_does_not_change_generated_code(package, raw):
    path = save(package, raw)
    generate_component(path)
    files = ("component_api.h", "component_state.h", "component_shell.c", "component_sources.cmake")
    before = {name: (package / "generated" / name).read_bytes() for name in files}
    raw["interfaces"]["out"]["drive_divide"] = 99
    save(package, raw)
    with pytest.raises(ComponentDefinitionError):
        check_generated(path)
    generate_component(path)
    for name in files:
        assert before[name] == (package / "generated" / name).read_bytes()


def test_existing_user_code_is_never_overwritten_on_first_generation(package, raw):
    path = save(package, raw)
    (package / "user").mkdir()
    (package / "user" / "algorithm.c").write_bytes(b"/* original algorithm */\n")
    (package / "CMakeLists.txt").write_bytes(b"# original build\n")
    generate_component(path)
    assert (package / "user" / "algorithm.c").read_bytes() == b"/* original algorithm */\n"
    assert (package / "CMakeLists.txt").read_bytes() == b"# original build\n"


def test_missing_configuration_and_generation_lock(package, raw):
    path = package / "missing.yaml"
    for action in (load_component_definition, generate_component, check_generated):
        with pytest.raises(ComponentDefinitionError) as error:
            action(path)
        assert error.value.diagnostic["code"] == "read_error"
    path = save(package, raw)
    (package / ".component_codegen.lock").write_bytes(b"")
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError) as error:
        generate_component(path)
    assert error.value.diagnostic["code"] == "generation_locked"
    assert snapshot(package) == before


@pytest.mark.parametrize("section,key,value", [
    ("top", "interfaces", []), ("top", "algorithm", []),
    ("top", "clock_source", 1), ("top", "abi_version", True),
    ("top", "abi_version", 6), ("top", "id", "invalid/id"),
    ("top", "id", "NULL"), ("interfaces", "gain", []),
    ("interfaces", "gain", {"type": [], "default": 1}),
])
def test_malformed_types_fail_with_structured_diagnostics(package, raw, section, key, value):
    (raw if section == "top" else raw["interfaces"])[key] = value
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError) as error:
        generate_component(path)
    assert isinstance(error.value.diagnostic["message"], str)
    assert snapshot(package) == before


def test_nested_duplicates_and_bad_references(package, raw):
    path = save(package, raw)
    path.write_text("id: test.pilot\nname: 测试\nversion: '1'\ninterfaces:\n"
                    "  in:\n    type: audio\n    direction: input\n    channels: 2\n    channels: 3\n",
                    encoding="utf-8", newline="\n")
    with pytest.raises(ComponentDefinitionError, match="重复键"):
        load_component_definition(path)
    raw["interfaces"]["in"]["channels"] = "out:channels"
    raw["interfaces"]["out"]["channels"] = "in:channels"
    with pytest.raises(ComponentDefinitionError, match="循环"):
        normalize_component_definition(raw)
    raw["interfaces"]["in"]["channels"] = "param:channels"
    raw["interfaces"]["out"]["channels"] = 2
    raw["interfaces"]["channels"]["default"] = 0
    raw["interfaces"]["channels"]["range"] = [0, 8]
    with pytest.raises(ComponentDefinitionError, match="正数"):
        normalize_component_definition(raw)


def test_schema_accepts_initial_declaration(raw):
    import jsonschema
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    jsonschema.validate(raw, schema)


def test_display_metadata_and_inherited_block_size(package, raw):
    import jsonschema
    metadata = {"category": "基础信号", "description": "中文组件说明", "clock_domain": "自定义时钟域"}
    raw.update(metadata)
    path = save(package, raw)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    definition = normalize_component_definition(load_component_definition(path))
    for key, value in metadata.items():
        assert definition[key] == value
    assert all("block_size" not in p for p in definition["ports"])
    generate_component(path)
    check_generated(path)
    shell = (package / "generated" / "component_shell.c").read_text(encoding="utf-8")
    assert shell.count(".block_size=0") == 2
    assert "io.out.buffer->frame_count=ctx->frame_count;" in shell


@pytest.mark.parametrize("key", ["category", "description", "clock_domain"])
@pytest.mark.parametrize("value", [True, 1, [], "", "带\x00字节"])
def test_invalid_display_metadata_rejected(raw, key, value):
    raw[key] = value
    with pytest.raises(ComponentDefinitionError):
        normalize_component_definition(raw)


def test_execution_metadata_preserved_and_validated(raw):
    import jsonschema
    raw.update({"order": 12, "platforms": ["win"], "ui_hidden": False,
                "memory": {"state_size": 0, "scratch_size": 64, "alignment": 16}})
    raw["execution"] = {"sample_rate_independent": True, "realtime_safe": True,
                        "supports_inplace": True, "latency_samples": 3}
    raw["interfaces"]["gain"].update({
        "update_policy": "smoothed", "bindable": True, "shape": [], "kind": "setting",
        "widget": "slider", "description": "平滑由算法或 Runtime 管理", "readonly": False,
        "options": [{"value": 1.0, "label": "单位增益"}],
    })
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    normalized = normalize_component_definition(raw)
    for key in ("order", "platforms", "ui_hidden", "execution", "memory"):
        assert normalized[key] == raw[key]
    assert normalized["parameters"][0]["shape"] == []
    assert normalized["parameters"][0]["bindable"] is True
    raw["execution"]["realtime_safe"] = "yes"
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    with pytest.raises(ComponentDefinitionError, match="execution.realtime_safe"):
        normalize_component_definition(raw)


@pytest.mark.parametrize("kind,default", [("float", 1.0), ("int", 1), ("bool", True), ("string", "")])
@pytest.mark.parametrize("extra", [{"direction": "output"}, {"process": True},
                                 {"direction": "output", "process": True}])
def test_scalar_direction_and_process_rejected_by_schema_and_core(package, raw, kind, default, extra):
    import jsonschema
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    raw["interfaces"]["scalar"] = {"type": kind, "default": default, **extra}
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    for action in (load_component_definition, generate_component):
        with pytest.raises(ComponentDefinitionError):
            action(path)
    with pytest.raises(ComponentDefinitionError):
        normalize_component_definition(raw)
    assert snapshot(package) == before


@pytest.fixture
def cmake_workspace():
    root = ROOT / "build" / f"component_cmake_{uuid.uuid4().hex}"
    root.mkdir(parents=True)
    yield root
    shutil.rmtree(root)


@pytest.mark.parametrize("mode,generator", [
    ("integrated", "Ninja"), ("integrated", "Ninja Multi-Config"),
    ("standalone", "Ninja"), ("missing_abi", "Ninja"),
])
def test_cmake_abi_target_fallback_and_runtime_output(cmake_workspace, raw, mode, generator):
    compiler = Path(r"C:\BuildTools\Strawberry\c\bin\gcc.exe")
    cmake, ninja = shutil.which("cmake"), shutil.which("ninja")
    if not compiler.exists() or not cmake or not ninja:
        pytest.skip("当前环境缺少 CMake、Ninja 或仓库指定的 C 编译器")
    root = cmake_workspace
    package = root / "package"
    package.mkdir()
    generate_component(save(package, raw))
    binary = root / "binary"
    source = package
    if mode == "integrated":
        source = root / "parent"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.16)\nproject(ComponentIntegration C)\n"
            f'add_subdirectory("{(ROOT / "orpheus_abi").as_posix()}" abi EXCLUDE_FROM_ALL)\n'
            f'add_subdirectory("{package.as_posix()}" package)\n'
            'add_executable(consumer consumer.c)\n'
            'target_link_libraries(consumer PRIVATE test_pilot)\n'
            'get_target_property(component_links test_pilot LINK_LIBRARIES)\n'
            'if(NOT "orpheus_abi" IN_LIST component_links)\n'
            '    message(FATAL_ERROR "组件必须链接已存在的 orpheus_abi target")\n'
            'endif()\n',
            encoding="utf-8", newline="\n")
        (source / "consumer.c").write_text(
            '#include "generated/component_state.h"\n'
            'int main(void) { return orpheus_get_interface()->get_descriptor()->abi_version != 7; }\n',
            encoding="utf-8", newline="\n")
    command = [cmake, "-S", str(source), "-B", str(binary), "-G", generator,
               f"-DCMAKE_C_COMPILER={compiler}", f"-DCMAKE_MAKE_PROGRAM={ninja}"]
    if mode == "standalone":
        command.append(f"-DORPHEUS_ABI_INCLUDE_DIR={ROOT / 'orpheus_abi' / 'include'}")
    elif mode == "integrated":
        # 即便 fallback 路径无效，也必须优先消费已有 target。
        command.append("-DORPHEUS_ABI_INCLUDE_DIR=nonexistent_abi")
    env = dict(os.environ)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    result = subprocess.run(command, env=env, capture_output=True, encoding="utf-8", errors="replace")
    if mode == "missing_abi":
        assert result.returncode != 0
        assert "ORPHEUS_ABI_INCLUDE_DIR" in result.stdout + result.stderr
        assert not (binary / "components").exists()
        return
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([cmake, "--build", str(binary), "--config", "Debug"],
                            env=env, capture_output=True, encoding="utf-8", errors="replace")
    assert result.returncode == 0, result.stdout + result.stderr
    components = binary / "components"
    libraries = list(components.glob("*test_pilot*.dll"))
    assert len(libraries) == 1
    assert list(components.glob("*test_pilot*.dll.a"))
    assert not (components / "Debug").exists()
    if mode == "integrated":
        env["PATH"] = str(components) + os.pathsep + env["PATH"]
        consumer = binary / ("Debug" if generator == "Ninja Multi-Config" else "") / "consumer.exe"
        result = subprocess.run([str(consumer)], env=env, capture_output=True, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr


def test_real_c_compiler_and_abi_harness(package, raw):
    compiler = Path(r"C:\BuildTools\Strawberry\c\bin\gcc.exe")
    if not compiler.exists():
        pytest.skip("当前环境没有仓库指定的 C 编译器")
    raw["interfaces"]["in"]["channels"] = "param:channels"
    raw["interfaces"]["out"]["channels"] = "in:channels"
    path = save(package, raw)
    generate_component(path)
    algorithm = package / "user" / "algorithm.c"
    algorithm.write_text(
        '#include "component_state.h"\n'
        'int test_pilot_algorithm(test_pilot_State* s, test_pilot_IO* io, const OrpheusProcessContext* ctx) {\n'
        '    if (!io->in.samples || !io->out.samples) return ORPHEUS_OK;\n'
        '    for (unsigned i=0; i<io->in.buffer->frame_count*2; ++i)\n'
        '        io->out.samples[i]=io->in.samples[i]*s->gain;\n'
        '    io->out.buffer->frame_count=io->in.buffer->frame_count;\n'
        '    return test_pilot_emit_out(ctx, &io->out);\n}\n',
        encoding="utf-8", newline="\n")
    harness = package / "harness.c"
    harness.write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
static unsigned emissions;
static int emit(void* user, uint32_t drive, const OrpheusDelivery* d) {
    assert(user == &emissions && drive == TEST_PILOT_DRIVE_OUT);
    assert(d->frame_index == 123 && d->epoch == 9 && d->timeline_flags == 1);
    assert(d->buffer->frame_count == 32 && d->buffer->channels == 2);
    ++emissions;
    return ORPHEUS_OK;
}
static OrpheusSlotId add(void* user, const OrpheusSlotInfo* slot) {
    unsigned* n=(unsigned*)user;
    assert(slot->offset < sizeof(test_pilot_State));
    ++*n; return *n;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    assert(api->get_descriptor()->abi_version == 7);
    assert(api->get_descriptor()->state_size == sizeof(test_pilot_State));
    assert(api->get_descriptor()->ports[0].block_size == 0);
    assert(api->get_descriptor()->ports[1].block_size == 0);
    test_pilot_State state; void* instance=NULL;
    OrpheusConfig config={.state_block=&state};
    assert(api->create(&instance,&config)==0 && instance==&state);
    assert(state.gain==1.0f && state.channels==2 && state.enabled);
    unsigned slots=0; OrpheusRegistry reg={.ctx=&slots,.add=add};
    assert(api->register_slots(instance,&reg)==0 && slots==3);
    OrpheusValue gain={.type=ORPHEUS_VALUE_FLOAT,.value.f32=2};
    assert(api->set_parameter(instance,"gain",&gain)==0);
    OrpheusValue got;
    assert(api->get_parameter(instance,"gain",&got)==0 && got.value.f32==2);
    gain.value.f32=99;
    assert(api->set_parameter(instance,"gain",&gain)==ORPHEUS_ERR_INVALID_ARG);
    float source[64], dest[64]; for(unsigned i=0;i<64;i++) source[i]=(float)i;
    OrpheusBuffer input={.data=source,.format=ORPHEUS_FORMAT_F32,.channels=2,
        .frame_capacity=32,.frame_count=32,.interleaved=true};
    OrpheusBuffer output=input; output.data=dest;
    const OrpheusBuffer* inputs[]={&input}; OrpheusBuffer* outputs[]={&output};
    OrpheusContinuation continuation={.user=&emissions,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=32,.frame_index=123,.epoch=9,.timeline_flags=1,.continuation=&continuation};
    assert(api->process(instance,&ctx)==0 && emissions==1);
    for(unsigned i=0;i<64;i++) assert(dest[i]==source[i]*2);
    input.frame_count=33;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG); input.frame_count=32;
    input.format=ORPHEUS_FORMAT_I16;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG); input.format=ORPHEUS_FORMAT_F32;
    output.frame_capacity=31; output.frame_count=0;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    inputs[0]=NULL; outputs[0]=NULL;
    assert(api->process(instance,&ctx)==0 && emissions==1);
    assert(api->reset(instance)==0 && state.gain==2);
    assert(api->destroy(instance)==0);
    return 0;
}
''', encoding="utf-8", newline="\n")
    executable = package / "harness.exe"
    command = [str(compiler), "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-DORPHEUS_BUILDING_COMPONENT", "-I", str(ROOT / "orpheus_abi" / "include"),
               "-I", "generated", "-I", "user",
               r"generated\component_shell.c", r"user\algorithm.c", "harness.c",
               "-o", "harness.exe"]
    result = subprocess.run(command, cwd=package, capture_output=True, encoding="utf-8", errors="replace")
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(executable)], capture_output=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("shape", ["sink", "source", "parameters", "override"])
def test_compile_default_template_and_unilateral_ports(package, raw, shape):
    compiler = Path(r"C:\BuildTools\Strawberry\c\bin\gcc.exe")
    if not compiler.exists():
        pytest.skip("当前环境没有仓库指定的 C 编译器")
    if shape == "sink":
        del raw["interfaces"]["out"]
    elif shape == "source":
        del raw["interfaces"]["in"]
        raw["interfaces"]["out"]["block_size"] = 32
    elif shape == "parameters":
        del raw["interfaces"]["in"], raw["interfaces"]["out"]
    else:
        raw["algorithm"] = {"prefix": "pilot"}
    path = save(package, raw)
    generate_component(path)
    symbols = normalize_component_definition(raw)["algorithm_symbols"]
    (package / "harness.c").write_text(
        '#include "component_state.h"\n#include <assert.h>\nint main(void) {\n'
        f'    {symbols["state_type"]} state; void* instance=0;\n'
        '    OrpheusConfig config={.state_block=&state}; OrpheusProcessContext ctx={0};\n'
        '    const OrpheusComponentInterface* api=orpheus_get_interface();\n'
        '    assert(api->create(&instance,&config)==0);\n'
        '    assert(api->process(instance,&ctx)==ORPHEUS_ERR_UNSUPPORTED);\n'
        '    return 0;\n}\n', encoding="utf-8", newline="\n")
    result = subprocess.run(
        [str(compiler), "-std=c11", "-Wall", "-Wextra", "-Werror", "-DORPHEUS_BUILDING_COMPONENT",
         "-I", str(ROOT / "orpheus_abi" / "include"), "-I", "generated", "-I", "user",
         r"generated\component_shell.c", r"user\algorithm.c", "harness.c", "-o", "harness.exe"],
        cwd=package, capture_output=True, encoding="utf-8", errors="replace")
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(package / "harness.exe")], capture_output=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("lifecycle", ["prepare", ["unknown"], ["prepare", "prepare"], [1], {"reset": True}])
def test_invalid_lifecycle_rejected(package, raw, lifecycle):
    raw["algorithm"] = {"lifecycle": lifecycle}
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("parameter", [
    {"type": "string", "default": "中文", "capacity": 6},
    {"type": "string", "default": "", "capacity": 0},
    {"type": "string", "default": "", "capacity": True},
    {"type": "string", "default": "", "capacity": 262145},
    {"type": "float", "default": 1, "capacity": 256},
    {"type": "float", "default": 1, "bindable": True, "update_policy": "transactional"},
    {"type": "float", "default": 1, "control_source": True, "access": "write"},
    {"type": "float", "default": 1, "shape": ["param:missing"]},
])
def test_string_and_control_metadata_validation(package, raw, parameter):
    raw["interfaces"]["extra"] = parameter
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


def test_lifecycle_generated_symbols_and_parameter_policies(package, raw):
    import jsonschema
    raw["algorithm"] = {"lifecycle": ["prepare", "destroy", "reset"]}
    raw["interfaces"]["gain"]["update_policy"] = "smoothed"
    raw["interfaces"]["enabled"]["update_policy"] = "transactional"
    raw["interfaces"]["text"] = {"type": "string", "default": "中文"}
    path = save(package, raw)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    generate_component(path)
    shell = (package / "generated" / "component_shell.c").read_text(encoding="utf-8")
    header = (package / "generated" / "component_state.h").read_text(encoding="utf-8")
    assert "char text[256];" in header
    assert "int test_pilot_prepare(test_pilot_State* state, const OrpheusConfig* config);" in header
    assert "int test_pilot_set_parameter(" not in header
    assert "test_pilot_get_parameter(" not in shell
    assert "ORPHEUS_UPDATE_SMOOTHED" in shell
    assert "ORPHEUS_UPDATE_TRANSACTIONAL" in shell
    assert "ORPHEUS_SLOT_DIRECT_WRITE" not in shell
    assert ".block_size=0" in shell
    assert_component_generated_current(path)


def _compile_component_harness(package):
    compiler = Path(r"C:\BuildTools\Strawberry\c\bin\gcc.exe")
    if not compiler.exists():
        pytest.skip("当前环境没有仓库指定的 C 编译器")
    command = [str(compiler), "-std=c11", "-Wall", "-Wextra", "-Werror", "-DORPHEUS_BUILDING_COMPONENT",
               "-I", str(ROOT / "orpheus_abi" / "include"), "-I", "generated", "-I", "user",
               r"generated\component_shell.c", r"user\algorithm.c", "harness.c", "-o", "harness.exe"]
    result = subprocess.run(command, cwd=package, capture_output=True, encoding="utf-8", errors="replace")
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(package / "harness.exe")], capture_output=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


def test_real_lifecycle_transactions_and_utf8_string_ownership(package, raw):
    raw["algorithm"] = {"lifecycle": [
        "prepare", "reset", "destroy", "set_parameter", "get_parameter", "register_slots", "hook"]}
    raw["interfaces"]["gain"]["update_policy"] = "smoothed"
    raw["interfaces"]["enabled"]["update_policy"] = "transactional"
    raw["interfaces"]["label"] = {"type": "string", "default": "初", "capacity": 8, "kind": "setting"}
    raw["interfaces"]["current_gain"] = {
        "type": "float", "default": 0.0, "kind": "probe", "persistent": False, "readonly": True,
        "readback": True, "control_source": True}
    raw["execution"] = {"realtime_safe": True, "supports_inplace": True, "latency_samples": 3}
    raw["memory"] = {"state_size": 0, "scratch_size": 64, "alignment": 16}
    path = save(package, raw)
    generate_component(path)
    (package / "user" / "algorithm.h").write_text(
        "#pragma once\n"
        "typedef struct test_pilot_AlgorithmState { int* resource; int counter; } test_pilot_AlgorithmState;\n",
        encoding="utf-8", newline="\n")
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int prepares, destroys, sets, gets, resets, registered;
char readback_text[8]="view";
int test_pilot_algorithm(test_pilot_State* s, test_pilot_IO* io, const OrpheusProcessContext* ctx) {
    (void)io; (void)ctx; ++s->algorithm.counter; return ORPHEUS_OK;
}
int test_pilot_prepare(test_pilot_State* s, const OrpheusConfig* config) {
    assert(s->algorithm.resource==NULL);
    ++prepares;
    s->algorithm.resource=(int*)malloc(sizeof(int));
    if (!s->algorithm.resource) return ORPHEUS_ERR_OUT_OF_MEMORY;
    *s->algorithm.resource=42; s->current_gain=7.0f;
    return config->sample_rate<0 ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK;
}
int test_pilot_reset(test_pilot_State* s) {
    ++resets; s->algorithm.counter=0; s->current_gain=0.0f; return ORPHEUS_OK;
}
int test_pilot_destroy(test_pilot_State* s) {
    ++destroys; free(s->algorithm.resource); s->algorithm.resource=NULL; return ORPHEUS_OK;
}
int test_pilot_set_parameter(test_pilot_State* s, const char* key, const OrpheusValue* value) {
    ++sets;
    if (strcmp(key,"gain")==0 && value->value.f32==3.0f) {
        s->gain=99.0f; s->enabled=false; s->current_gain=99.0f;
        return ORPHEUS_ERR_PROCESSING;
    }
    if (strcmp(key,"gain")==0) s->current_gain=12.0f;
    return ORPHEUS_OK;
}
int test_pilot_get_parameter(test_pilot_State* s, const char* key, OrpheusValue* value) {
    (void)s; ++gets;
    if (strcmp(key,"label")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=readback_text; return ORPHEUS_OK;
    }
    if (strcmp(key,"enabled")==0) return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_ERR_NOT_FOUND;
}
int test_pilot_register_slots(test_pilot_State* s, const OrpheusRegistry* reg) {
    assert(registered==5);
    return ORPHEUS_REG_SLOT(reg,s,algorithm.counter,ORPHEUS_SLOT_PROBE,"counter","计数",
        ORPHEUS_VALUE_INT,.flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID
        ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK;
}
int test_pilot_hook(test_pilot_State* s,uint32_t id,uint32_t event,const OrpheusBlob* req,OrpheusBlob* resp) {
    assert(s && id==8 && event==ORPHEUS_EVENT_CUSTOM && req && req->len==4);
    if (resp) resp->len=17;
    return ORPHEUS_HOOK_HANDLED;
}
''', encoding="utf-8", newline="\n")
    users = {p.name: p.read_bytes() for p in (package / "user").iterdir()}
    generate_component(path)
    assert users == {p.name: p.read_bytes() for p in (package / "user").iterdir()}
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
extern int prepares,destroys,sets,gets,resets,registered;
extern char readback_text[8];
static OrpheusSlotId add(void* user,const OrpheusSlotInfo* slot) {
    (void)user; ++registered;
    assert(!(slot->flags & ORPHEUS_SLOT_DIRECT_WRITE));
    if (strcmp(slot->key,"label")==0) {
        assert(slot->type==ORPHEUS_VALUE_STRING && slot->size==8 && slot->count==1);
    }
    if (strcmp(slot->key,"current_gain")==0) {
        assert(slot->kind==ORPHEUS_SLOT_PROBE && slot->type==ORPHEUS_VALUE_FLOAT);
        assert(!(slot->flags & ORPHEUS_SLOT_PERSISTENT));
    }
    assert(slot->offset+slot->size*slot->count<=sizeof(test_pilot_State));
    return registered;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    assert(api->get_descriptor()->scratch_size==64 && api->get_descriptor()->alignment>=16);
    assert(api->get_descriptor()->supports_inplace && api->get_descriptor()->latency_samples==3);
    assert(api->get_descriptor()->params[0].update_policy==ORPHEUS_UPDATE_SMOOTHED);
    assert(api->get_descriptor()->params[2].update_policy==ORPHEUS_UPDATE_TRANSACTIONAL);
    _Alignas(16) test_pilot_State state; void* instance=NULL;
    char config_label[8]="config";
    const char* ids[]={"label","gain"};
    OrpheusValue vals[]={{.type=ORPHEUS_VALUE_STRING,.value.str=config_label},
                        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=2.0f}};
    OrpheusConfig config={.state_block=&state,.channels=3,.param_count=2,.param_ids=ids,.param_values=vals};
    assert(api->create(&instance,&config)==0 && prepares==0 && destroys==0 && sets==0);
    assert(state.channels==3 && state.gain==2.0f && strcmp(state.label,"config")==0);
    config_label[0]='X'; assert(strcmp(state.label,"config")==0);
    config_label[0]='c';
    assert(api->prepare(instance,&config)==0 && prepares==1 && destroys==0 && sets==0);
    assert(state.current_gain==7.0f && *state.algorithm.resource==42);
    config_label[0]='X'; assert(strcmp(state.label,"config")==0);
    OrpheusRegistry reg={.add=add};
    assert(api->register_slots(instance,&reg)==0 && registered==6);
    OrpheusValue v={.type=ORPHEUS_VALUE_FLOAT,.value.f32=3.0f};
    assert(api->set_parameter(instance,"gain",&v)==ORPHEUS_ERR_PROCESSING);
    assert(state.gain==2.0f && state.enabled && state.current_gain==7.0f);
    v.value.f32=2.5f;
    assert(api->set_parameter(instance,"gain",&v)==0 && state.gain==2.5f && state.current_gain==12.0f);
    int calls_before_probe=sets;
    assert(api->set_parameter(instance,"current_gain",&v)==ORPHEUS_ERR_UNSUPPORTED);
    assert(sets==calls_before_probe && state.current_gain==12.0f);
    int calls=sets; v.value.f32=99;
    assert(api->set_parameter(instance,"gain",&v)==ORPHEUS_ERR_INVALID_ARG && sets==calls);
    v.type=ORPHEUS_VALUE_INT; v.value.i32=2;
    assert(api->set_parameter(instance,"gain",&v)==ORPHEUS_ERR_INVALID_ARG && sets==calls);
    char own[8]="中文"; v.type=ORPHEUS_VALUE_STRING; v.value.str=own;
    assert(api->set_parameter(instance,"label",&v)==0);
    own[0]='X'; assert(strcmp(state.label,"中文")==0);
    calls=sets; v.value.str="中文呀";
    assert(api->set_parameter(instance,"label",&v)==ORPHEUS_ERR_INVALID_ARG && sets==calls);
    v.value.str="\xC0\xAF";
    assert(api->set_parameter(instance,"label",&v)==ORPHEUS_ERR_INVALID_ARG && sets==calls);
    v.value.str="\xED\xA0\x80";
    assert(api->set_parameter(instance,"label",&v)==ORPHEUS_ERR_INVALID_ARG && sets==calls);
    v.value.str=NULL;
    assert(api->set_parameter(instance,"label",&v)==ORPHEUS_ERR_INVALID_ARG && sets==calls);
    OrpheusValue got;
    assert(api->get_parameter(instance,"gain",&got)==0 && got.value.f32==2.5f);
    assert(api->get_parameter(instance,"enabled",&got)==ORPHEUS_ERR_PROCESSING);
    assert(api->get_parameter(instance,"label",&got)==0 && strcmp(got.value.str,"view")==0);
    assert(got.value.str==state._orpheus_readback_label);
    readback_text[0]='X'; assert(strcmp(got.value.str,"view")==0);
    assert(strcmp(state.label,"中文")==0);
    state.algorithm.counter=12;
    assert(api->reset(instance)==0 && resets==1 && state.algorithm.counter==0);
    assert(state.gain==2.5f && state.channels==3 && state.current_gain==0.0f);
    assert(strcmp(state.label,"中文")==0 && *state.algorithm.resource==42);
    uint32_t data=7; OrpheusBlob req={.data=&data,.len=4},resp={0};
    assert(api->hook(instance,8,ORPHEUS_EVENT_CUSTOM,&req,&resp)==ORPHEUS_HOOK_HANDLED && resp.len==17);
    assert(api->prepare(instance,&config)==0 && prepares==2 && destroys==1);
    assert(api->destroy(instance)==0 && destroys==2);
    assert(api->destroy(instance)==0 && destroys==2);
    assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG);
    config.sample_rate=-1;
    assert(api->create(&instance,&config)==0 && prepares==2);
    assert(api->prepare(instance,&config)==ORPHEUS_ERR_PROCESSING && prepares==3 && destroys==3);
    assert(state.algorithm.resource==NULL);
    assert(api->destroy(instance)==0 && destroys==3);
    config.sample_rate=48000;
    assert(api->create(&instance,&config)==0);
    config.channels=99;
    assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG && prepares==3);
    assert(api->destroy(instance)==0 && destroys==3);
    return 0;
}
''', encoding="utf-8", newline="\n")
    _compile_component_harness(package)


@pytest.mark.parametrize("port_type,format_name,c_type,format_enum", [
    ("control", "i32", "int32_t", "ORPHEUS_FORMAT_I32"),
    ("bulk", "i16", "int16_t", "ORPHEUS_FORMAT_I16"),
    ("debug", "f32", "float", "ORPHEUS_FORMAT_F32"),
])
def test_real_non_audio_typed_buffer_views(package, raw, port_type, format_name, c_type, format_enum):
    raw["interfaces"]["in"].update({"type": port_type, "sample_format": format_name, "channels": 1, "block_size": 2})
    raw["interfaces"]["out"].update({"type": port_type, "sample_format": format_name, "channels": "in:channels"})
    path = save(package, raw)
    generate_component(path)
    (package / "user" / "algorithm.c").write_text(
        '#include "component_state.h"\n'
        'int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {\n'
        '    (void)s;\n'
        '    if (!io->in.samples || !io->out.samples) return ORPHEUS_OK;\n'
        '    for (unsigned i=0;i<ctx->frame_count;i++) io->out.samples[i]=io->in.samples[i];\n'
        '    return test_pilot_emit_out(ctx,&io->out);\n}\n', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(
        '#include "component_state.h"\n#include <assert.h>\nint main(void) {\n'
        '    const OrpheusComponentInterface* api=orpheus_get_interface();\n'
        f'    assert(api->get_descriptor()->ports[0].type==ORPHEUS_PORT_{port_type.upper()});\n'
        '    test_pilot_State state;void* instance=0;OrpheusConfig config={.state_block=&state};\n'
        '    assert(api->create(&instance,&config)==0);\n'
        f'    {c_type} src[2]={{3,7}},dst[2]={{0}};\n'
        f'    OrpheusBuffer in={{.data=src,.format={format_enum},.channels=1,.frame_count=2,.frame_capacity=2,.interleaved=true}};\n'
        '    OrpheusBuffer out=in;out.data=dst;\n'
        '    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};\n'
        '    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=2};\n'
        '    assert(api->process(instance,&ctx)==0 && dst[0]==3 && dst[1]==7);\n'
        '    return 0;\n}\n', encoding="utf-8", newline="\n")
    _compile_component_harness(package)


@pytest.mark.parametrize("field,value", [
    ("sample_format", "f32"), ("channels", 1), ("block_size", 32), ("sample_rate", 48000),
    ("layout", "pcm"), ("frame_size", 0), ("timing", "sync"),
])
def test_trigger_rejects_pcm_fields_in_schema_and_core(package, raw, field, value):
    import jsonschema
    raw["interfaces"]["tick"] = {"type": "trigger", "direction": "output", "drive_port_no": 2, field: value}
    path = save(package, raw)
    before = snapshot(package)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


def test_real_trigger_delivery_views_and_explicit_emission(package, raw):
    import jsonschema
    raw["interfaces"]["kick"] = {"type": "trigger", "direction": "input", "process": True}
    raw["interfaces"]["other"] = {"type": "trigger", "direction": "input"}
    raw["interfaces"]["tick"] = {"type": "trigger", "direction": "output", "drive_port_no": 2, "drive_divide": 999}
    path = save(package, raw)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    normalized = normalize_component_definition(raw)
    ports = {p["id"]: p for p in normalized["ports"]}
    assert ports["kick"]["propagate_drive"] is True
    assert ports["other"]["propagate_drive"] is False
    assert ports["tick"]["propagate_drive"] is True
    assert "channels" not in ports["kick"] and "sample_format" not in ports["tick"]
    generate_component(path)
    (package / "user" / "algorithm.h").write_text(
        "#pragma once\ntypedef struct test_pilot_AlgorithmState { unsigned calls; } test_pilot_AlgorithmState;\n",
        encoding="utf-8", newline="\n")
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
int test_pilot_algorithm(test_pilot_State* state,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    assert(io->kick.delivery==(ctx->trigger_input==1 ? ctx->delivery : NULL));
    assert(io->other.delivery==(ctx->trigger_input==2 ? ctx->delivery : NULL));
    if (io->in.samples && io->out.samples) io->out.samples[0]=io->in.samples[0];
    if (ctx->timestamp<0) {
        io->tick.delivery.buffer=io->in.buffer;
        return test_pilot_emit_tick(ctx,&io->tick);
    }
    if (++state->algorithm.calls%2) return ORPHEUS_OK;
    return test_pilot_emit_tick(ctx,&io->tick);
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
static unsigned emissions;
static int emit(void* user,uint32_t drive,const OrpheusDelivery* delivery) {
    (void)user; assert(drive==TEST_PILOT_DRIVE_TICK && !delivery->buffer);
    assert(delivery->frame_index==100 && delivery->epoch==3 && delivery->timeline_flags==1);
    ++emissions; return ORPHEUS_OK;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    assert(api->get_descriptor()->ports[2].type==ORPHEUS_PORT_TRIGGER);
    assert(api->get_descriptor()->ports[2].channels==0);
    test_pilot_State state;void* instance=NULL;OrpheusConfig config={.state_block=&state};
    assert(api->create(&instance,&config)==0);
    float data[2]={5,7},dest[2]={0};
    OrpheusBuffer input={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=2,.frame_count=1,
        .frame_capacity=1,.interleaved=true},output=input;output.data=dest;
    const OrpheusBuffer* inputs[]={&input,NULL,NULL};
    OrpheusBuffer* outputs[]={&output,NULL};
    OrpheusDelivery delivery={.buffer=NULL,.frame_index=101};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=3,.output_count=2,
        .frame_count=1,.frame_index=100,.epoch=3,.timeline_flags=1,.trigger_input=1,
        .delivery=&delivery,.continuation=&continuation};
    assert(api->process(instance,&ctx)==0 && emissions==0 && dest[0]==5);
    assert(api->process(instance,&ctx)==0 && emissions==1);
    ctx.trigger_input=2;
    assert(api->process(instance,&ctx)==0 && emissions==1);
    ctx.trigger_input=0;
    assert(api->process(instance,&ctx)==0 && emissions==2);
    ctx.timestamp=-1;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && emissions==2);
    ctx.timestamp=0;ctx.trigger_input=1;delivery.buffer=&input;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    delivery.buffer=NULL;inputs[1]=&input;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    inputs[1]=NULL;outputs[1]=&output;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    return 0;
}
''', encoding="utf-8", newline="\n")
    _compile_component_harness(package)


def test_compile_trigger_only_and_empty_parameters(package):
    raw = {"id": "test.pilot", "name": "纯触发", "version": "1",
           "interfaces": {"tick": {"type": "trigger", "direction": "output", "drive_port_no": 1}}}
    generate_component(save(package, raw))
    (package / "harness.c").write_text(
        '#include "component_state.h"\n#include <assert.h>\nint main(void) {\n'
        '    const OrpheusComponentInterface* api=orpheus_get_interface();\n'
        '    test_pilot_State state;void* instance=NULL;OrpheusConfig config={.state_block=&state};\n'
        '    assert(api->create(&instance,&config)==0);\n'
        '    OrpheusProcessContext ctx={0};\n'
        '    assert(api->prepare(instance,&config)==0);\n'
        '    assert(api->process(instance,&ctx)==ORPHEUS_ERR_UNSUPPORTED);\n'
        '    assert(api->reset(instance)==0 && api->destroy(instance)==0);\n'
        '    return 0;\n}\n', encoding="utf-8", newline="\n")
    _compile_component_harness(package)


def test_compile_control_characters_and_trigraph_string_default(package, raw):
    raw["interfaces"]["text"] = {"type": "string", "default": "中文\1??/\\\"\n"}
    path = save(package, raw)
    generate_component(path)
    (package / "harness.c").write_text(
        '#include "component_state.h"\n#include <assert.h>\n#include <string.h>\nint main(void) {\n'
        '    test_pilot_State state;void* instance=NULL;OrpheusConfig config={.state_block=&state};\n'
        '    assert(orpheus_get_interface()->create(&instance,&config)==0);\n'
        '    assert(strcmp(state.text,"中文\\001\\?\\?/\\\\\\"\\012")==0);\n'
        '    OrpheusValue got;assert(orpheus_get_interface()->get_parameter(instance,"text",&got)==0);\n'
        '    assert(got.type==ORPHEUS_VALUE_STRING && got.value.str==state.text);\n'
        '    return 0;\n}\n', encoding="utf-8", newline="\n")
    _compile_component_harness(package)


def test_compile_all_default_lifecycle_stubs_and_get_fallback(package, raw):
    raw["algorithm"] = {"lifecycle": ["prepare", "reset", "destroy", "set_parameter",
                                    "get_parameter", "register_slots", "hook"]}
    raw["interfaces"]["text"] = {"type": "string", "default": "中文"}
    generate_component(save(package, raw))
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;OrpheusConfig config={.state_block=&state};
    assert(api->create(&instance,&config)==0 && api->prepare(instance,&config)==0);
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=2};
    assert(api->set_parameter(instance,"gain",&value)==0 && state.gain==2);
    assert(api->get_parameter(instance,"gain",&value)==0 && value.value.f32==2);
    assert(api->get_parameter(instance,"text",&value)==0 && strcmp(value.value.str,"中文")==0);
    assert(value.value.str==state.text);
    assert(api->hook(instance,0,ORPHEUS_EVENT_CUSTOM,NULL,NULL)==ORPHEUS_HOOK_CONTINUE);
    assert(api->reset(instance)==0 && state.gain==2 && api->destroy(instance)==0);
    return 0;
}
''', encoding="utf-8", newline="\n")
    _compile_component_harness(package)


def test_default_reset_closes_user_resources_without_losing_parameters(package, raw):
    raw["algorithm"] = {"lifecycle": ["prepare", "destroy"]}
    generate_component(save(package, raw))
    (package / "user" / "algorithm.h").write_text(
        "#pragma once\ntypedef struct test_pilot_AlgorithmState { int* resource; } test_pilot_AlgorithmState;\n",
        encoding="utf-8", newline="\n")
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <stdlib.h>
unsigned allocations,destructions;
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;(void)io;(void)ctx;return ORPHEUS_OK;
}
int test_pilot_prepare(test_pilot_State* s,const OrpheusConfig* config) {
    (void)config;++allocations;s->algorithm.resource=(int*)malloc(sizeof(int));
    return s->algorithm.resource ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
int test_pilot_destroy(test_pilot_State* s) {
    ++destructions;free(s->algorithm.resource);return ORPHEUS_OK;
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
extern unsigned allocations,destructions;
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;OrpheusConfig config={.state_block=&state};
    assert(api->create(&instance,&config)==0 && api->prepare(instance,&config)==0);
    assert(allocations==1 && destructions==0 && state.algorithm.resource);
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=2};
    assert(api->set_parameter(instance,"gain",&value)==0);
    assert(api->reset(instance)==0 && destructions==1 && state.algorithm.resource==NULL && state.gain==2);
    assert(api->prepare(instance,&config)==0 && allocations==2 && destructions==1);
    assert(api->destroy(instance)==0 && destructions==2);
    assert(api->destroy(instance)==0 && destructions==2);
    return 0;
}
''', encoding="utf-8", newline="\n")
    _compile_component_harness(package)


def _add_bulk_test_interfaces(raw, manual=False):
    raw["interfaces"]["gains"] = {
        "type": "string", "default": "1,1", "kind": "bulk", "shape": ["param:channels"],
        "update_policy": "restart_required",
        "bulk": {"sample_format": "f32", "capacity": 4, "storage": "algorithm.per_channel_gains",
                 "double_bank": True, "auto_register": not manual, "range": [-2.0, 2.0]},
    }
    raw["interfaces"]["weights"] = {
        "type": "float", "count": 3, "default": [0.0, 0.5, 1.0], "range": [-1.0, 1.0],
        "shape": [3], "bulk": {"double_bank": True}, "update_policy": "restart_required",
    }
    raw["interfaces"]["steps"] = {
        "type": "int", "count": 2, "default": [1, 2], "range": [0, 8], "update_policy": "restart_required",
    }
    raw["interfaces"]["switches"] = {
        "type": "bool", "count": 2, "default": [True, False], "update_policy": "restart_required",
    }


def test_bulk_metadata_has_one_fact_source_and_generated_private_storage(package, raw):
    import jsonschema
    _add_bulk_test_interfaces(raw)
    raw.update({"scheduling": {"divisor": "param:channels", "input_block_size": "2*param:channels",
                               "history": False, "split": True, "merge": False, "async_bridge": True},
                "clock_source": True, "clock_domain": "file", "platforms": ["win"]})
    before = copy.deepcopy(raw)
    normalized = normalize_component_definition(raw)
    assert raw == before
    resources = {slot["id"]: slot for slot in normalized["bulk_slots"]}
    assert resources["gains"] == {
        "id": "gains", "name": "gains", "type": "float", "count": 4,
        "shape": ["param:channels"], "double_bank": True, "access": "read_write", "range": [-2.0, 2.0],
    }
    assert resources["weights"]["count"] == 3 and resources["weights"]["double_bank"]
    assert resources["steps"]["type"] == "int" and resources["switches"]["type"] == "bool"
    assert normalized["scheduling"] == raw["scheduling"]
    assert normalized["clock_source"] and normalized["clock_domain"] == "file"
    assert normalized["platforms"] == ["win"]
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    path = save(package, raw)
    generate_component(path)
    header = (package / "user" / "algorithm.h").read_text(encoding="utf-8")
    assert "float per_channel_gains[4];" in header
    state_header = (package / "generated" / "component_state.h").read_text(encoding="utf-8")
    assert "float weights[3];" in state_header and "int32_t steps[2];" in state_header
    assert "bool switches[2];" in state_header
    shell = (package / "generated" / "component_shell.c").read_text(encoding="utf-8")
    assert "ORPHEUS_REG_SLOT(reg, s, gains," not in shell
    assert "ORPHEUS_REG_ARRAY(reg, s, algorithm.per_channel_gains," in shell
    assert "ORPHEUS_SLOT_DOUBLE_BUFFERED" in shell
    assert "_Static_assert(_Generic" in shell
    assert_component_generated_current(path)
    raw["bulk_slots"] = []
    with pytest.raises(ComponentDefinitionError, match="bulk_slots"):
        normalize_component_definition(raw)


def test_decoder_dependency_survives_normalization_and_cli_generation(package, raw):
    import jsonschema
    raw["deps"] = ["miniaudio"]
    before = copy.deepcopy(raw)
    normalized = normalize_component_definition(raw)
    assert normalized["deps"] == ["miniaudio"] and raw == before
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    generate_component(save(package, raw))
    cmake = (package / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "ORPHEUS_MINIAUDIO_INCLUDE_DIR" in cmake
    assert 'message(FATAL_ERROR "请设置 ORPHEUS_MINIAUDIO_INCLUDE_DIR")' in cmake


@pytest.mark.parametrize("dependencies", ["miniaudio", [1], [True], [None], ["../library"],
                                         ["lib;other"], [""], ["miniaudio", "miniaudio"]])
def test_invalid_dependency_declarations_are_rejected(raw, dependencies):
    raw["deps"] = dependencies
    with pytest.raises(ComponentDefinitionError, match="依赖"):
        normalize_component_definition(raw)


@pytest.mark.parametrize("field,value", [
    ("storage", "algorithm.values[0]"), ("storage", "algorithm->values"),
    ("storage", "algorithm.values + 1"), ("storage", "user.values"),
    ("storage", "algorithm.float"), ("capacity", 0), ("capacity", True), ("capacity", 65537),
    ("sample_format", "i16"), ("double_bank", 1), ("auto_register", 0),
    ("range", [2, -2]), ("range", [0, float("nan")]), ("range", [True, 1]),
])
def test_invalid_bulk_binding_is_readonly(package, raw, field, value):
    _add_bulk_test_interfaces(raw)
    raw["interfaces"]["gains"]["bulk"][field] = value
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("parameter", [
    {"type": "float", "count": 2, "default": 1.0},
    {"type": "float", "count": 2, "default": [1.0]},
    {"type": "float", "count": 2, "default": [1.0, True]},
    {"type": "float", "count": 2, "default": [1.0, float("nan")]},
    {"type": "int", "count": 2, "default": [1, 2.0]},
    {"type": "bool", "count": 2, "default": [True, 1]},
    {"type": "string", "count": 2, "default": ["a", "b"]},
    {"type": "float", "count": "param:channels", "default": [1, 2]},
    {"type": "float", "count": 2, "default": [1, 2], "range": [0, 1]},
    {"type": "float", "count": 2, "default": [1, 2], "shape": [3]},
    {"type": "float", "count": 2, "default": [1, 2], "bulk": {"capacity": 2}},
    {"type": "float", "count": 2, "default": [1, 2], "bulk": {"sample_format": "i32"}},
])
def test_fixed_array_declarations_are_strict_and_readonly(package, raw, parameter):
    raw["interfaces"]["array"] = parameter
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


def test_bulk_shape_capacity_storage_overlap_and_schedule_references(raw):
    _add_bulk_test_interfaces(raw)
    raw["interfaces"]["gains"]["shape"] = [5]
    with pytest.raises(ComponentDefinitionError, match="容量"):
        normalize_component_definition(raw)
    raw["interfaces"]["gains"]["shape"] = ["param:channels"]
    raw["interfaces"]["other"] = copy.deepcopy(raw["interfaces"]["gains"])
    with pytest.raises(ComponentDefinitionError, match="同一存储"):
        normalize_component_definition(raw)
    del raw["interfaces"]["other"]
    raw["scheduling"] = {"divisor": "system('bad')"}
    with pytest.raises(ComponentDefinitionError, match="调度量"):
        normalize_component_definition(raw)
    raw["scheduling"] = {"input_block_size": "2*param:missing"}
    with pytest.raises(ComponentDefinitionError, match="调度引用"):
        normalize_component_definition(raw)


@pytest.mark.parametrize("key", ["async_bridge", "merge"])
@pytest.mark.parametrize("value", [0, "true", [], None])
def test_scheduling_bridge_flags_require_boolean(raw, key, value):
    import jsonschema
    raw["scheduling"] = {key: value}
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    with pytest.raises(ComponentDefinitionError):
        normalize_component_definition(raw)


def test_real_bulk_private_binding_arrays_shadow_banks_and_lifecycle(package, raw):
    _add_bulk_test_interfaces(raw)
    raw["algorithm"] = {"lifecycle": ["prepare", "reset", "destroy"]}
    path = save(package, raw)
    generate_component(path)
    (package / "user" / "algorithm.h").write_text(
        "#pragma once\ntypedef struct test_pilot_AlgorithmState {\n"
        "    float per_channel_gains[4]; unsigned history;\n} test_pilot_AlgorithmState;\n",
        encoding="utf-8", newline="\n")
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <stdlib.h>
#include <math.h>
unsigned prepares,destroys,resets;
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)ctx;++s->algorithm.history;
    if (io->in.samples && io->out.samples)
        io->out.samples[0]=io->in.samples[0]*s->algorithm.per_channel_gains[0]*s->weights[0];
    return ORPHEUS_OK;
}
int test_pilot_prepare(test_pilot_State* s,const OrpheusConfig* config) {
    (void)config;++prepares;
    const char* text=s->gains;
    for (int32_t i=0;i<s->channels;i++) {
        char* end;float value=strtof(text,&end);
        if (end==text || !isfinite(value) || value < -2 || value > 2) return ORPHEUS_ERR_INVALID_ARG;
        s->algorithm.per_channel_gains[i]=value;
        if (i+1<s->channels) { if (*end!=',') return ORPHEUS_ERR_INVALID_ARG;text=end+1; }
        else if (*end) return ORPHEUS_ERR_INVALID_ARG;
    }
    return ORPHEUS_OK;
}
int test_pilot_reset(test_pilot_State* s) { ++resets;s->algorithm.history=0;return ORPHEUS_OK; }
int test_pilot_destroy(test_pilot_State* s) { (void)s;++destroys;return ORPHEUS_OK; }
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
#include <math.h>
extern unsigned prepares,destroys,resets;
typedef struct { OrpheusSlotInfo info;unsigned char shadow[64];bool pending; } Slot;
static Slot slots[16];static unsigned used;static test_pilot_State* state;
static OrpheusSlotId add(void* user,const OrpheusSlotInfo* info) {
    (void)user;
    for(unsigned i=0;i<used;i++) assert(strcmp(slots[i].info.key,info->key)!=0);
    assert(info->offset+info->size*info->count<=sizeof(*state));
    slots[used].info=*info;
    assert(info->size*info->count<=sizeof(slots[used].shadow));
    memcpy(slots[used].shadow,(char*)state+info->offset,info->size*info->count);
    return used++;
}
static Slot* find(const char* key) {
    for(unsigned i=0;i<used;i++) if(strcmp(slots[i].info.key,key)==0) return &slots[i];
    assert(0);return NULL;
}
static int write_bulk(const char* key,const void* values,unsigned count) {
    Slot* slot=find(key);OrpheusSlotInfo* info=&slot->info;
    if(info->kind!=ORPHEUS_SLOT_BULK || count>info->count) return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned i=0;i<count;i++) {
        if(info->type==ORPHEUS_VALUE_FLOAT) {
            float value=((const float*)values)[i];
            if(!isfinite(value)||value<info->min_f32||value>info->max_f32) return ORPHEUS_ERR_INVALID_ARG;
        } else if(info->type==ORPHEUS_VALUE_INT) {
            int32_t value=((const int32_t*)values)[i];
            if(value<info->min_i32||value>info->max_i32) return ORPHEUS_ERR_INVALID_ARG;
        }
    }
    if(info->flags & ORPHEUS_SLOT_DOUBLE_BUFFERED) {
        memmove(slot->shadow,values,count*info->size);slot->pending=true;
    } else memmove((char*)state+info->offset,values,count*info->size);
    return ORPHEUS_OK;
}
static void commit(void) {
    for(unsigned i=0;i<used;i++) if(slots[i].pending) {
        memcpy((char*)state+slots[i].info.offset,slots[i].shadow,slots[i].info.size*slots[i].info.count);
        slots[i].pending=false;
    }
}
static void read_bulk(const char* key,void* out,unsigned count) {
    Slot* slot=find(key);assert(count<=slot->info.count);
    memcpy(out,(char*)state+slot->info.offset,count*slot->info.size);
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();test_pilot_State memory;state=&memory;
    void* instance=NULL;OrpheusConfig config={.state_block=state};
    assert(api->create(&instance,&config)==0);
    assert(state->weights[1]==0.5f&&state->steps[1]==2&&state->switches[0]&&!state->switches[1]);
    const char* keys[]={"weights","steps","switches"};
    OrpheusValue vals[]={{.type=ORPHEUS_VALUE_STRING,.value.str="0.25, 0.5, 0.75"},
                        {.type=ORPHEUS_VALUE_STRING,.value.str="3,4"},
                        {.type=ORPHEUS_VALUE_STRING,.value.str="false,true"}};
    config.param_ids=keys;config.param_values=vals;config.param_count=3;
    assert(api->prepare(instance,&config)==0&&prepares==1&&destroys==0);
    assert(state->weights[0]==0.25f&&state->steps[0]==3&&!state->switches[0]&&state->switches[1]);
    OrpheusRegistry reg={.add=add};
    assert(api->register_slots(instance,&reg)==0&&used==7);
    assert(find("gains")->info.kind==ORPHEUS_SLOT_BULK&&find("gains")->info.type==ORPHEUS_VALUE_FLOAT);
    assert(find("gains")->info.count==2&&find("gains")->info.size==sizeof(float));
    assert(find("gains")->info.offset==offsetof(test_pilot_State,algorithm.per_channel_gains));
    assert(find("weights")->info.kind==ORPHEUS_SLOT_BULK&&find("weights")->info.count==3);
    assert(find("switches")->info.type==ORPHEUS_VALUE_BOOL&&find("switches")->info.size==sizeof(bool));
    float gains[]={2,0.5f},weights[]={0.5f,0.25f,0.75f},result[2]={0};
    assert(write_bulk("gains",gains,2)==0&&write_bulk("weights",weights,3)==0);
    read_bulk("gains",result,2);assert(result[0]==1);
    assert(state->weights[0]==0.25f);
    float invalid[]={3,0};
    assert(write_bulk("gains",invalid,2)==ORPHEUS_ERR_INVALID_ARG);
    assert(write_bulk("gains",gains,3)==ORPHEUS_ERR_INVALID_ARG);
    commit();read_bulk("gains",result,2);assert(result[0]==2&&state->weights[0]==0.5f);
    int32_t steps[]={5,6};bool switches[]={true,false};
    assert(write_bulk("steps",steps,2)==0&&state->steps[0]==5);
    assert(write_bulk("switches",switches,2)==0&&state->switches[0]&&!state->switches[1]);
    OrpheusValue scalar={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0};
    assert(api->set_parameter(instance,"weights",&scalar)==ORPHEUS_ERR_UNSUPPORTED);
    assert(api->get_parameter(instance,"weights",&scalar)==ORPHEUS_ERR_UNSUPPORTED);
    assert(api->get_parameter(instance,"gains",&scalar)==0&&strcmp(scalar.value.str,"1,1")==0);
    const char* bad[]={"0.5,1","0.5,1,0,1","0.5,1,","nan,0,0","2,0,0","1e999,0,0"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        vals[0].value.str=bad[i];
        assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG&&prepares==1&&destroys==0);
        assert(state->weights[0]==0.5f&&state->algorithm.per_channel_gains[0]==2);
    }
    vals[0].value.str="0.25,0.5,0.75";vals[1].value.str="1.5,2";
    assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG&&prepares==1);
    vals[1].value.str="3,4";vals[2].value.str="2,true";
    assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG&&prepares==1);
    vals[2].value.str="false,true";config.channels=5;
    assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG&&prepares==1);
    config.channels=2;
    state->algorithm.history=9;
    assert(api->reset(instance)==0&&resets==1&&state->algorithm.history==0);
    assert(state->weights[0]==0.5f&&state->algorithm.per_channel_gains[0]==2);
    assert(api->destroy(instance)==0&&destroys==1);
    const char* csvkey[]={"gains"};OrpheusValue csv={.type=ORPHEUS_VALUE_STRING,.value.str="bad"};
    config.param_ids=csvkey;config.param_values=&csv;config.param_count=1;
    assert(api->create(&instance,&config)==0);
    assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG&&prepares==2&&destroys==2);
    assert(api->destroy(instance)==0&&destroys==2);
    return 0;
}
''', encoding="utf-8", newline="\n")
    _compile_component_harness(package)


def test_real_manual_bulk_registration_uses_only_generated_metadata(package, raw):
    _add_bulk_test_interfaces(raw, manual=True)
    raw["interfaces"]["mode"] = {"type": "int", "default": 0}
    raw["algorithm"] = {"lifecycle": ["register_slots"]}
    path = save(package, raw)
    generate_component(path)
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;(void)io;(void)ctx;return ORPHEUS_OK;
}
int test_pilot_register_slots(test_pilot_State* s,const OrpheusRegistry* reg) {
    return s->mode==2 ? test_pilot_register_gains(s,reg) : ORPHEUS_OK;
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
static unsigned gains,others;
static OrpheusSlotId add(void* user,const OrpheusSlotInfo* slot) {
    (void)user;
    if(strcmp(slot->key,"gains")==0) {
        ++gains;
        assert(slot->kind==ORPHEUS_SLOT_BULK&&slot->type==ORPHEUS_VALUE_FLOAT&&slot->count==3);
        assert(slot->flags&ORPHEUS_SLOT_DOUBLE_BUFFERED);
        assert(slot->offset==offsetof(test_pilot_State,algorithm.per_channel_gains));
    } else ++others;
    return gains+others;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;OrpheusConfig config={.state_block=&state,.channels=3};
    OrpheusRegistry reg={.add=add};
    assert(api->create(&instance,&config)==0&&api->register_slots(instance,&reg)==0);
    assert(gains==0&&others==7);
    state.mode=2;assert(api->register_slots(instance,&reg)==0&&gains==1);
    state.channels=5;
    assert(test_pilot_register_gains(&state,&reg)==ORPHEUS_ERR_INVALID_ARG&&gains==1);
    return 0;
}
''', encoding="utf-8", newline="\n")
    _compile_component_harness(package)


def test_nested_private_bulk_storage_template_compiles(package, raw):
    _add_bulk_test_interfaces(raw)
    raw["interfaces"]["gains"]["bulk"]["storage"] = "algorithm.coefficients.active"
    generate_component(save(package, raw))
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
static unsigned gains;
static OrpheusSlotId add(void* user,const OrpheusSlotInfo* slot) {
    (void)user;
    if(strcmp(slot->key,"gains")==0) {
        ++gains;
        assert(slot->offset==offsetof(test_pilot_State,algorithm.coefficients.active));
        assert(slot->count==3 && slot->size==sizeof(float));
    }
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;OrpheusConfig config={.state_block=&state,.channels=3};
    assert(api->create(&instance,&config)==0);
    OrpheusRegistry reg={.add=add};
    assert(api->register_slots(instance,&reg)==0 && gains==1);
    assert(api->get_descriptor()->params[4].type==ORPHEUS_VALUE_FLOAT);
    assert(api->get_descriptor()->params[4].default_value.type==ORPHEUS_VALUE_STRING);
    assert(strcmp(api->get_descriptor()->params[4].default_value.value.str,"0.0,0.5,1.0")==0);
    return 0;
}
''', encoding="utf-8", newline="\n")
    _compile_component_harness(package)


@pytest.mark.parametrize("storage_declaration", [
    "float per_channel_gains[3];", "float* per_channel_gains;",
    "int32_t per_channel_gains[4];", "float per_channel_gains[4][1];",
])
def test_c_compiler_rejects_wrong_private_bulk_storage(package, raw, storage_declaration):
    compiler = Path(r"C:\BuildTools\Strawberry\c\bin\gcc.exe")
    if not compiler.exists():
        pytest.skip("当前环境没有仓库指定的 C 编译器")
    _add_bulk_test_interfaces(raw)
    path = save(package, raw)
    generate_component(path)
    (package / "user" / "algorithm.h").write_text(
        '#pragma once\n#include <stdint.h>\n'
        f'typedef struct test_pilot_AlgorithmState {{ {storage_declaration} }} test_pilot_AlgorithmState;\n',
        encoding="utf-8", newline="\n")
    result = subprocess.run(
        [str(compiler), "-std=c11", "-fsyntax-only", "-DORPHEUS_BUILDING_COMPONENT",
         "-I", str(ROOT / "orpheus_abi" / "include"), "-I", "generated", "-I", "user",
         r"generated\component_shell.c"],
        cwd=package, capture_output=True, encoding="utf-8", errors="replace")
    assert result.returncode != 0
    assert "BULK gains" in result.stderr and "storage" in result.stderr


@pytest.mark.parametrize("reference", ["task:block_size", "task:sample_rate"])
def test_task_signature_references_preserved_and_executable(package, raw, reference):
    import jsonschema
    raw["interfaces"]["in"]["channels"] = reference
    raw["interfaces"]["in"]["block_size"] = "task:block_size"
    raw["interfaces"]["in"]["sample_rate"] = "task:sample_rate"
    raw["interfaces"]["out"]["channels"] = "in:channels"
    raw["interfaces"]["out"]["block_size"] = "task:block_size"
    raw["interfaces"]["out"]["sample_rate"] = "task:sample_rate"
    path = save(package, raw)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    normalized = normalize_component_definition(load_component_definition(path))
    assert normalized["ports"][0]["channels"] == reference
    assert normalized["ports"][1]["block_size"] == "task:block_size"
    generate_component(path)
    assert_component_generated_current(path)
    (package / "user" / "algorithm.c").write_text(
        '#include "component_state.h"\n'
        'int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {\n'
        '    (void)s;\n'
        '    if (!io->in.samples || !io->out.samples) return ORPHEUS_OK;\n'
        '    for (unsigned i=0;i<ctx->frame_count*io->in.buffer->channels;i++) io->out.samples[i]=io->in.samples[i];\n'
        '    return test_pilot_emit_out(ctx,&io->out);\n}\n', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    assert(api->get_descriptor()->ports[0].channels==0);
    assert(api->get_descriptor()->ports[0].sample_rate==0);
    assert(api->get_descriptor()->ports[1].block_size==0);
    test_pilot_State state;void* instance=NULL;
    OrpheusConfig config={.state_block=&state,.block_size=2,.sample_rate=2};
    assert(api->create(&instance,&config)==0 && api->prepare(instance,&config)==0);
    float data[4]={1,2,3,4},output_data[4]={0};
    OrpheusBuffer in={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=2,.frame_count=2,
        .frame_capacity=2,.interleaved=true},out=in;out.data=output_data;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=2,.sample_rate=2};
    assert(api->process(instance,&ctx)==0 && output_data[0]==1 && output_data[3]==4);
    return 0;
}
''', encoding="utf-8", newline="\n")
    _compile_component_harness(package)


@pytest.mark.parametrize("reference", ["task:channels", "task:frames", "task:unknown"])
def test_unsupported_task_fields_rejected_without_side_effects(package, raw, reference):
    import jsonschema
    raw["interfaces"]["out"]["block_size"] = reference
    path = save(package, raw)
    before = snapshot(package)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("alignment", [None, 1, 8, 16, 32])
@pytest.mark.parametrize("private_alignment", [None, 16, 32])
def test_state_layout_alignment_matches_descriptor_in_c_and_cpp(package, raw, alignment, private_alignment):
    if alignment is not None:
        raw["memory"] = {"alignment": alignment}
    generate_component(save(package, raw))
    if private_alignment:
        (package / "user" / "algorithm.h").write_text(
            "#pragma once\n"
            "typedef struct test_pilot_AlgorithmState {\n"
            "#if defined(__cplusplus)\n"
            f"    alignas({private_alignment}) unsigned char history;\n"
            "#else\n"
            f"    _Alignas({private_alignment}) unsigned char history;\n"
            "#endif\n"
            "} test_pilot_AlgorithmState;\n",
            encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>
#ifdef __cplusplus
#define ALIGNOF(T) alignof(T)
#else
#define ALIGNOF(T) _Alignof(T)
#endif
typedef struct LegacyState {
    float phase,frequency,amplitude;
    uint32_t channels,sample_rate;
} LegacyState;
typedef struct StateGroup {
    LegacyState before;
    test_pilot_State first;
    LegacyState between;
    test_pilot_State second;
} StateGroup;
typedef struct Arena {
    char prefix[3];
    StateGroup group;
} Arena;
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    const OrpheusComponentDescriptor* descriptor=api->get_descriptor();
    assert(ALIGNOF(test_pilot_State)>=EXPECTED_ALIGNMENT);
    assert(ALIGNOF(test_pilot_AlgorithmState)>=EXPECTED_PRIVATE_ALIGNMENT);
    assert(ALIGNOF(test_pilot_State)>=ALIGNOF(test_pilot_AlgorithmState));
    assert(descriptor->alignment==ALIGNOF(test_pilot_State));
    assert(sizeof(test_pilot_State)%descriptor->alignment==0);
    assert(sizeof(LegacyState)==20);
    assert((offsetof(Arena,group)+offsetof(StateGroup,first))%descriptor->alignment==0);
    assert((offsetof(Arena,group)+offsetof(StateGroup,second))%descriptor->alignment==0);
    Arena arena;
    OrpheusConfig config;
    memset(&config,0,sizeof(config));
    config.channels=2;config.block_size=32;config.sample_rate=48000;
    test_pilot_State* states[]={&arena.group.first,&arena.group.second};
    for (unsigned i=0;i<2;++i) {
        void* instance=NULL;
        assert((uintptr_t)states[i]%descriptor->alignment==0);
        config.state_block=states[i];
        assert(api->create(&instance,&config)==ORPHEUS_OK);
        assert(instance==states[i] && api->prepare(instance,&config)==ORPHEUS_OK);
        assert(api->destroy(instance)==ORPHEUS_OK);
    }
    return 0;
}
'''.replace("EXPECTED_ALIGNMENT", str(alignment or 1)).replace(
        "EXPECTED_PRIVATE_ALIGNMENT", str(private_alignment or 1)), encoding="utf-8", newline="\n")
    _compile_component_harness(package)
    compiler = Path(r"C:\BuildTools\Strawberry\c\bin\gcc.exe")
    cpp_compiler = compiler.with_name("g++.exe")
    if not cpp_compiler.exists():
        pytest.skip("当前环境没有仓库指定的 C++ 编译器")
    options = ["-Wall", "-Wextra", "-Werror", "-DORPHEUS_BUILDING_COMPONENT",
               "-I", str(ROOT / "orpheus_abi" / "include"), "-I", "generated", "-I", "user"]
    commands = [
        [str(compiler), "-std=c11", *options, "-c",
         r"generated\component_shell.c", r"user\algorithm.c"],
        [str(cpp_compiler), "-std=c++11", *options, "harness.c",
         "component_shell.o", "algorithm.o", "-o", "cpp_harness.exe"],
        [str(package / "cpp_harness.exe")],
    ]
    for command in commands:
        result = subprocess.run(command, cwd=package, capture_output=True, encoding="utf-8", errors="replace")
        assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("private_storage", [False, True])
@pytest.mark.parametrize("manual", [False, True])
def test_bulk_slot_key_preserves_two_dotted_resource_ids_in_native_c(package, raw, private_storage, manual):
    import jsonschema
    raw["interfaces"] = {}
    for index in range(2):
        binding = {"slot_key": f"bq{index}.coefs", "double_bank": True, "auto_register": not manual}
        item = {"type": "float", "count": 5, "default": [0.0] * 5, "range": [-4, 4],
                "update_policy": "restart_required", "bulk": binding}
        if private_storage:
            binding.update({"sample_format": "f32", "capacity": 5,
                            "storage": f"algorithm.stage{index}.coefs", "range": [-4, 4]})
            item = {"type": "string", "default": "", "kind": "bulk",
                    "update_policy": "restart_required", "bulk": binding}
        raw["interfaces"][f"bq{index}_coefs"] = item
    before = copy.deepcopy(raw)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    definition = normalize_component_definition(raw)
    assert raw == before
    assert [p["id"] for p in definition["parameters"]] == ["bq0_coefs", "bq1_coefs"]
    assert [p["id"] for p in definition["bulk_slots"]] == ["bq0.coefs", "bq1.coefs"]
    assert all(p["type"] == "float" and p["count"] == 5 and p["double_bank"]
               and "slot_key" not in p for p in definition["bulk_slots"])
    assert [(p["id"], p["slot_key"]) for p in definition["bulk_bindings"]] == [
        ("bq0_coefs", "bq0.coefs"), ("bq1_coefs", "bq1.coefs")]
    path = save(package, raw)
    generate_component(path)
    state_header = (package / "generated" / "component_state.h").read_text(encoding="utf-8")
    assert "test_pilot_register_bq0_coefs(" in state_header
    assert "test_pilot_register_bq1_coefs(" in state_header
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
typedef struct Slot { OrpheusSlotInfo info; float shadow[5]; } Slot;
static Slot slots[2];
static unsigned used;
static test_pilot_State* state;
static OrpheusSlotId add(void* user,const OrpheusSlotInfo* info) {
    (void)user;
    assert(used<2 && info->kind==ORPHEUS_SLOT_BULK && info->type==ORPHEUS_VALUE_FLOAT);
    assert(info->count==5 && info->size==sizeof(float));
    assert(info->flags&ORPHEUS_SLOT_DOUBLE_BUFFERED);
    assert(info->min_f32==-4 && info->max_f32==4);
    assert(info->offset+info->size*info->count<=sizeof(*state));
    assert(strcmp(info->key,used ? "bq1.coefs" : "bq0.coefs")==0);
    slots[used].info=*info;
    memcpy(slots[used].shadow,(char*)state+info->offset,sizeof(slots[used].shadow));
    return used++;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State memory;state=&memory;
    void* instance=NULL;
    OrpheusConfig config={.state_block=state};
    assert(api->create(&instance,&config)==ORPHEUS_OK);
    assert(api->prepare(instance,&config)==ORPHEUS_OK);
    OrpheusRegistry reg={.add=add};
    assert(api->register_slots(instance,&reg)==ORPHEUS_OK);
    if (MANUAL) {
        assert(used==0);
        assert(test_pilot_register_bq0_coefs(state,&reg)==ORPHEUS_OK);
        assert(test_pilot_register_bq1_coefs(state,&reg)==ORPHEUS_OK);
    }
    assert(used==2);
    assert(slots[0].info.offset==offsetof(test_pilot_State,STORAGE0));
    assert(slots[1].info.offset==offsetof(test_pilot_State,STORAGE1));
    assert(slots[0].info.offset!=slots[1].info.offset);
    const float values[2][5]={{1,2,3,4,-1},{-1,-2,-3,-4,1}};
    for (unsigned i=0;i<2;++i) {
        assert(((float*)((char*)state+slots[i].info.offset))[0]==0);
        memcpy(slots[i].shadow,values[i],sizeof(values[i]));
        assert(((float*)((char*)state+slots[i].info.offset))[0]==0);
    }
    for (unsigned i=0;i<2;++i)
        memcpy((char*)state+slots[i].info.offset,slots[i].shadow,sizeof(slots[i].shadow));
    for (unsigned i=0;i<2;++i) {
        float readback[5];
        memcpy(readback,(char*)state+slots[i].info.offset,sizeof(readback));
        assert(memcmp(readback,values[i],sizeof(readback))==0);
    }
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1};
    assert(api->set_parameter(instance,"bq0.coefs",&value)==ORPHEUS_ERR_NOT_FOUND);
    assert(api->get_parameter(instance,"bq1.coefs",&value)==ORPHEUS_ERR_NOT_FOUND);
    assert(api->reset(instance)==ORPHEUS_OK);
    if (PRIVATE_STORAGE) assert(state->STORAGE0[0]==0 && state->STORAGE1[0]==0);
    else assert(state->STORAGE0[0]==1 && state->STORAGE1[0]==-1);
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
'''.replace("MANUAL", str(int(manual))).replace("PRIVATE_STORAGE", str(int(private_storage))).replace(
        "STORAGE0", "algorithm.stage0.coefs" if private_storage else "bq0_coefs").replace(
        "STORAGE1", "algorithm.stage1.coefs" if private_storage else "bq1_coefs"),
        encoding="utf-8", newline="\n")
    _compile_component_harness(package)
    check_generated(path)


@pytest.mark.parametrize("slot_key", [
    None, True, 1, "", ".coefs", "bq0.", "bq0..coefs", "bq0.coefs[0]", "bq0->coefs",
    "bq0/coefs", "bq0 coefs", "bq0.coefs\n", "bq0.\"coefs", "0bq.coefs", "bq0.系数",
])
def test_invalid_bulk_slot_key_is_rejected_by_schema_and_core_without_writes(package, raw, slot_key):
    import jsonschema
    _add_bulk_test_interfaces(raw)
    raw["interfaces"]["gains"]["bulk"]["slot_key"] = slot_key
    path = save(package, raw)
    before = snapshot(package)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("collision", ["gain", "channels", "weights", "bq0.coefs"])
def test_bulk_slot_key_collisions_are_rejected_without_writes(package, raw, collision):
    _add_bulk_test_interfaces(raw)
    raw["interfaces"]["gains"]["bulk"]["slot_key"] = collision
    if collision == "bq0.coefs":
        raw["interfaces"]["weights"]["bulk"]["slot_key"] = collision
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError, match="注册键重复"):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("kind,value,expected", [
    ("float", [1, 0.125, -2.0], "1.0,0.125,-2.0"),
    ("int", [-2147483648, 0, 2147483647], "-2147483648,0,2147483647"),
    ("bool", [True, False, True], "1,0,1"),
    ("float", [0.25], "0.25"), ("int", [2], "2"), ("bool", [False], "0"),
])
def test_encode_array_initializer_is_strict_canonical_and_nonmutating(kind, value, expected):
    parameter = {"id": "weights", "type": kind, "count": len(value)}
    before = copy.deepcopy((parameter, value))
    assert is_array_parameter(parameter)
    assert encode_array_initializer(parameter, value) == expected
    assert (parameter, value) == before
    if kind != "bool":
        parameter["range"] = [min(value), max(value)]
        assert encode_array_initializer(parameter, value) == expected


@pytest.mark.parametrize("parameter,value", [
    (None, [1]), ([], [1]), ({"type": []}, [1]),
    ({"type": "float"}, [1]), ({"type": "string", "count": 1}, ["1"]),
    ({"type": "float", "count": 0}, []), ({"type": "int", "count": True}, [1]),
    ({"type": "int", "count": 65537}, [1]), ({"type": "int", "count": "param:n"}, [1]),
    ({"type": "float", "count": 1}, 1), ({"type": "float", "count": 1}, "1"),
    ({"type": "int", "count": 1}, (1,)), ({"type": "float", "count": 1}, []),
    ({"type": "float", "count": 1}, [1, 2]), ({"type": "float", "count": 1}, [True]),
    ({"type": "int", "count": 1}, [False]), ({"type": "bool", "count": 1}, [1]),
    ({"type": "bool", "count": 1}, ["true"]), ({"type": "int", "count": 1}, [1.0]),
    ({"type": "float", "count": 1}, [float("nan")]),
    ({"type": "float", "count": 1}, [float("inf")]),
    ({"type": "float", "count": 1}, [-float("inf")]),
    ({"type": "float", "count": 1}, [10 ** 400]),
    ({"type": "float", "count": 1}, [3.5e38]),
    ({"type": "int", "count": 1}, [2147483648]),
    ({"type": "int", "count": 1}, [-2147483649]),
    ({"type": "float", "count": 1, "range": [0, 1]}, [-0.5]),
    ({"type": "int", "count": 1, "range": [0, 1]}, [2]),
    ({"type": "float", "count": 1, "range": None}, [0]),
    ({"type": "float", "count": 1, "range": [1, 0]}, [0]),
    ({"type": "float", "count": 1, "range": [False, 1]}, [0]),
    ({"type": "float", "count": 1, "range": [0, float("inf")]}, [0]),
    ({"type": "int", "count": 1, "range": [0, 1.0]}, [0]),
    ({"type": "bool", "count": 1, "range": [0, 1]}, [False]),
])
def test_encode_array_initializer_errors_are_component_diagnostics(parameter, value):
    with pytest.raises(ComponentDefinitionError) as error:
        encode_array_initializer(parameter, value, location="nodes.bank.params.weights")
    assert error.value.diagnostic["location"].startswith("nodes.bank.params.weights")
    assert error.value.diagnostic["message"]


def test_array_initializer_diagnostic_identifies_bad_element():
    with pytest.raises(ComponentDefinitionError) as error:
        encode_array_initializer({"id": "weights", "type": "float", "count": 2}, [1, True])
    assert error.value.diagnostic["location"] == "parameters.weights[1]"


@pytest.mark.parametrize("kind,default,override,bad", [
    ("float", 0.5, 1.25, "nan"), ("int", 1, 2, "1.5"), ("bool", True, False, "2"),
])
@pytest.mark.parametrize("count", [1, 2])
def test_explicit_numeric_count_including_one_uses_shared_csv_and_native_array_state(
        package, raw, kind, default, override, bad, count):
    import jsonschema
    parameter = {"type": kind, "count": count, "default": [default] * count,
                 "update_policy": "restart_required"}
    if kind != "bool":
        parameter["range"] = [0, 2]
    raw["interfaces"] = {"values": parameter}
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    normalized = normalize_component_definition(raw)
    assert normalized["bulk_slots"][0]["count"] == count
    path = save(package, raw)
    generate_component(path)
    default_csv = encode_array_initializer(parameter, parameter["default"])
    override_csv = encode_array_initializer(parameter, [override] * count)
    header = (package / "generated" / "component_state.h").read_text(encoding="utf-8")
    assert f"values[{count}];" in header
    shell = (package / "generated" / "component_shell.c").read_text(encoding="utf-8")
    assert f".value.str={json.dumps(default_csv)}" in shell
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
static unsigned registrations;
static OrpheusSlotId add(void* user,const OrpheusSlotInfo* slot) {
    (void)user;++registrations;
    assert(slot->kind==ORPHEUS_SLOT_BULK && slot->count==COUNT);
    assert(slot->offset==offsetof(test_pilot_State,values));
    assert(strcmp(slot->key,"values")==0);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    assert(api->get_descriptor()->params[0].default_value.type==ORPHEUS_VALUE_STRING);
    assert(strcmp(api->get_descriptor()->params[0].default_value.value.str,DEFAULT_CSV)==0);
    test_pilot_State state;void* instance=NULL;
    OrpheusConfig config={.state_block=&state};
    assert(api->create(&instance,&config)==ORPHEUS_OK);
    for (unsigned i=0;i<COUNT;i++) assert(state.values[i]==DEFAULT_VALUE);
    const char* keys[]={"values"};
    OrpheusValue value={.type=ORPHEUS_VALUE_STRING,.value.str=OVERRIDE_CSV};
    config.param_ids=keys;config.param_values=&value;config.param_count=1;
    assert(api->prepare(instance,&config)==ORPHEUS_OK);
    for (unsigned i=0;i<COUNT;i++) assert(state.values[i]==OVERRIDE_VALUE);
    OrpheusRegistry reg={.add=add};
    assert(api->register_slots(instance,&reg)==ORPHEUS_OK && registrations==1);
    value.value.str=BAD_CSV;
    assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG);
    for (unsigned i=0;i<COUNT;i++) assert(state.values[i]==OVERRIDE_VALUE);
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
'''.replace("COUNT", str(count)).replace("DEFAULT_CSV", json.dumps(default_csv)).replace(
        "OVERRIDE_CSV", json.dumps(override_csv)).replace("BAD_CSV", json.dumps(",".join([bad] * count))).replace(
        "DEFAULT_VALUE", str(int(default)) if kind == "bool" else str(default)).replace(
        "OVERRIDE_VALUE", str(int(override)) if kind == "bool" else str(override)),
        encoding="utf-8", newline="\n")
    _compile_component_harness(package)


@pytest.mark.parametrize("kind", ["float", "int", "bool"])
def test_explicit_count_one_rejects_scalar_default_without_writes(package, raw, kind):
    raw["interfaces"] = {"values": {"type": kind, "count": 1, "default": False if kind == "bool" else 0}}
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


def _add_port_group_test_interfaces(raw):
    def audio(direction, **fields):
        return {"type": "audio", "direction": direction, "channels": 1, **fields}
    raw["interfaces"] = {
        "head": audio("input", process=True),
        "out_head": audio("output", drive_port_no=1),
        "ins": audio("input", count="param:n", optional=True, process=True),
        "items": audio("output", count="param:m", optional=True, drive_port_no=10, drive_divide=999),
        "tail": audio("input", process=True),
        "out_tail": audio("output", drive_port_no=20),
        "n": {"type": "int", "default": 1, "range": [0, 3], "update_policy": "restart_required"},
        "m": {"type": "int", "default": 1, "range": [0, 3], "update_policy": "restart_required"},
    }


@pytest.mark.parametrize("count,optional,bounds", [
    (0, True, (0, 0, 0)), (1, False, (1, 1, 1)), (3, False, (3, 3, 3)),
    ("param:m", True, (0, 3, 1)), ("param:m", False, (1, 3, 1)),
])
def test_port_group_normalization_and_bounded_api_are_single_source(package, raw, count, optional, bounds):
    import jsonschema
    _add_port_group_test_interfaces(raw)
    raw["interfaces"]["items"].update({"count": count, "optional": optional})
    if isinstance(count, str) and not optional:
        raw["interfaces"]["m"]["range"] = [1, 3]
    before = copy.deepcopy(raw)
    normalized = normalize_component_definition(raw)
    assert raw == before
    group = next(p for p in normalized["ports"] if p["id"] == "items")
    assert group["count"] == count
    assert (group["count_min"], group["count_max"], group["count_default"]) == bounds
    assert group.get("count_param") == ("m" if isinstance(count, str) else None)
    assert next(p for p in normalized["parameters"] if p["id"] == "n")["affects_signature"]
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    path = save(package, raw)
    generate_component(path)
    api = (package / "generated" / "component_api.h").read_text(encoding="utf-8")
    assert f"test_pilot_OutputF32 items[{max(1, bounds[1])}];" in api
    assert "uint32_t items_count;" in api
    assert "uint32_t input_index;" in api and "uint32_t drive_port_no;" in api
    assert "#define TEST_PILOT_DRIVE_ITEMS_BASE UINT32_C(10)" in api
    assert "TEST_PILOT_INPUT_" not in api
    check_generated(path)


@pytest.mark.parametrize("count,optional", [
    (0, False), (-1, True), (True, True), (1.5, True), (4294967295, True),
    ("param:missing", True), ("head:channels", True), ("task:block_size", True),
    ("param:n*2", True), ("1", True), (None, True), ([], True),
])
def test_invalid_port_count_declarations_are_readonly(package, raw, count, optional):
    _add_port_group_test_interfaces(raw)
    raw["interfaces"]["ins"].update({"count": count, "optional": optional})
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("parameter,optional", [
    ({"type": "int", "default": 1, "update_policy": "restart_required"}, True),
    ({"type": "int", "default": 1, "range": [0, 3], "update_policy": "restart_required"}, False),
    ({"type": "int", "default": 1, "range": [-1, 3], "update_policy": "restart_required"}, True),
    ({"type": "int", "default": 1, "range": [1, 3]}, True),
    ({"type": "int", "default": 1, "range": [1, 3], "update_policy": "immediate"}, True),
    ({"type": "int", "default": 1, "range": [1, 3], "update_policy": "block_boundary"}, True),
    ({"type": "float", "default": 1, "range": [1, 3], "update_policy": "restart_required"}, True),
    ({"type": "int", "count": 1, "default": [1], "range": [1, 3],
      "update_policy": "restart_required"}, True),
])
def test_port_count_requires_bounded_restart_int_scalar(package, raw, parameter, optional):
    _add_port_group_test_interfaces(raw)
    raw["interfaces"]["n"] = parameter
    raw["interfaces"]["ins"]["optional"] = optional
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("base,maximum,other,valid", [
    (10, 3, 12, False), (10, 3, 13, True),
    (4294967294, 1, 20, True), (4294967294, 2, 20, False),
    (4294967293, 2, 20, True), (4294967293, 3, 20, False),
    (10, 2147483647, 2147483650, False),
])
def test_port_drive_reserved_intervals_use_maximum_not_default(package, raw, base, maximum, other, valid):
    _add_port_group_test_interfaces(raw)
    raw["interfaces"]["items"]["drive_port_no"] = base
    raw["interfaces"]["out_tail"]["drive_port_no"] = other
    raw["interfaces"]["m"].update({"default": 0, "range": [0, maximum]})
    path = save(package, raw)
    before = snapshot(package)
    if valid:
        load_component_definition(path)
    else:
        with pytest.raises(ComponentDefinitionError):
            generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("other,other_count,maximum,collision", [
    ("items0", None, 1, True), ("items2", None, 3, True), ("items3", None, 3, False),
    ("items00", None, 3, False), ("items1", 1, 11, True), ("items1", 1, 10, False),
    ("items0", 2, 100, False), ("items1", 0, 100, False),
    ("items" + "9" * 100, None, 2147483647, False),
])
def test_port_expanded_id_collisions_are_checked_without_expansion(
        package, raw, other, other_count, maximum, collision):
    _add_port_group_test_interfaces(raw)
    raw["interfaces"]["items"].pop("drive_port_no")
    raw["interfaces"]["items"].pop("drive_divide")
    raw["interfaces"]["m"]["range"] = [0, maximum]
    raw["interfaces"][other] = {"type": "audio", "direction": "output"}
    if other_count is not None:
        raw["interfaces"][other].update({"count": other_count, "optional": True})
    path = save(package, raw)
    before = snapshot(package)
    if collision:
        with pytest.raises(ComponentDefinitionError, match="接口 ID"):
            generate_component(path)
    else:
        load_component_definition(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("other,direction,valid", [
    ("items_count", "input", False), ("items_count", "output", False),
    ("items_count", None, True), ("items_base", "output", False),
])
def test_port_group_generated_field_and_drive_constant_collisions(package, raw, other, direction, valid):
    _add_port_group_test_interfaces(raw)
    raw["interfaces"][other] = ({"type": "audio", "direction": direction, "drive_port_no": 30}
                               if direction == "output" else
                               {"type": "audio", "direction": direction} if direction else
                               {"type": "int", "default": 1})
    path = save(package, raw)
    if valid:
        generate_component(path)
    else:
        before = snapshot(package)
        with pytest.raises(ComponentDefinitionError, match="冲突"):
            generate_component(path)
        assert snapshot(package) == before


def test_huge_bounded_group_generation_has_constant_size_and_slot_overflow_is_rejected(package, raw):
    _add_port_group_test_interfaces(raw)
    raw["interfaces"]["n"]["range"] = [0, 2147483647]
    path = save(package, raw)
    generate_component(path)
    assert (package / "generated" / "component_shell.c").stat().st_size < 50000
    assert "ins[2147483647]" in (package / "generated" / "component_api.h").read_text(encoding="utf-8")
    raw["interfaces"]["ins"]["count"] = 4294967294
    save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError, match="槽位数"):
        generate_component(path)
    assert snapshot(package) == before


def _compile_cpp_component_algorithm_harness(package):
    compiler = Path(r"C:\BuildTools\Strawberry\c\bin\gcc.exe")
    cpp_compiler = compiler.with_name("g++.exe")
    if not compiler.exists() or not cpp_compiler.exists():
        pytest.skip("当前环境没有仓库指定的 C/C++ 编译器")
    options = ["-Wall", "-Wextra", "-Werror", "-DORPHEUS_BUILDING_COMPONENT",
               "-I", str(ROOT / "orpheus_abi" / "include"), "-I", "generated", "-I", "user"]
    commands = [
        [str(compiler), "-std=c11", *options, "-c", r"generated\component_shell.c", "harness.c"],
        [str(cpp_compiler), "-std=c++11", *options, "-x", "c++", "-c",
         r"user\algorithm.c", "-o", "algorithm_cpp.o"],
        [str(cpp_compiler), "component_shell.o", "algorithm_cpp.o", "harness.o", "-o", "cpp_harness.exe"],
        [str(package / "cpp_harness.exe")],
    ]
    for command in commands:
        result = subprocess.run(command, cwd=package, capture_output=True, encoding="utf-8", errors="replace")
        assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("cpp", [False, True])
def test_real_port_groups_map_actual_input_slots_and_independent_output_drives(package, raw, cpp):
    _add_port_group_test_interfaces(raw)
    path = save(package, raw)
    generate_component(path)
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
unsigned calls;
static void copy_samples(test_pilot_InputF32* input,test_pilot_OutputF32* output,
        const OrpheusProcessContext* ctx) {
    if (!output->samples) return;
    for (unsigned f=0;f<ctx->frame_count;f++)
        output->samples[f]=input->samples ? input->samples[f] : -99;
}
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    ++calls;
    assert(io->ins_count==(unsigned)s->n && io->items_count==(unsigned)s->m);
    assert(io->head.input_index==0 && io->tail.input_index==io->ins_count+1);
    assert(io->out_head.drive_port_no==1 && io->out_tail.drive_port_no==20);
    for(unsigned i=0;i<io->ins_count;i++) assert(io->ins[i].input_index==i+1);
    if (io->items_count<3) {
        assert(!io->items[io->items_count].active);
        assert(test_pilot_emit_items(ctx,&io->items[io->items_count])==ORPHEUS_ERR_INVALID_ARG);
    }
    copy_samples(&io->head,&io->out_head,ctx);
    assert(test_pilot_emit_out_head(ctx,&io->out_head)==ORPHEUS_OK);
    for(unsigned i=0;i<io->items_count;i++) {
        assert(io->items[i].drive_port_no==10+i);
        copy_samples(i<io->ins_count ? &io->ins[i] : &io->head,&io->items[i],ctx);
        assert(test_pilot_emit_items(ctx,&io->items[i])==ORPHEUS_OK);
        test_pilot_OutputF32 wrong=io->items[i];wrong.drive_port_no=20;
        assert(test_pilot_emit_items(ctx,&wrong)==ORPHEUS_ERR_INVALID_ARG);
    }
    copy_samples(&io->tail,&io->out_tail,ctx);
    assert(test_pilot_emit_out_tail(ctx,&io->out_tail)==ORPHEUS_OK);
    return ORPHEUS_OK;
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
extern unsigned calls;
static unsigned n,m,emissions;static bool null_outputs;
static OrpheusBuffer* expected_outputs[5];
static int emit(void* user,uint32_t drive,const OrpheusDelivery* delivery) {
    (void)user;
    unsigned index=drive==1 ? 0 : drive==20 ? m+1 : drive-10+1;
    assert(index<m+2 && (drive==1 || drive==20 || (drive>=10 && drive<10+m)));
    assert(delivery->buffer==(null_outputs ? NULL : expected_outputs[index]));
    assert(delivery->frame_index==123 && delivery->epoch==7 && delivery->timeline_flags==1);
    if (delivery->buffer) assert(delivery->buffer->frame_count==2);
    ++emissions;return ORPHEUS_OK;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;
    const char* keys[]={"n","m"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT},{.type=ORPHEUS_VALUE_INT}};
    OrpheusConfig config={.state_block=&state,.param_ids=keys,.param_values=values,.param_count=2};
    assert(api->create(&instance,&config)==ORPHEUS_OK);
    float source[5][2],dest[5][2];
    OrpheusBuffer inputs[5],outputs[5];
    const OrpheusBuffer* in[5];
    for(unsigned i=0;i<5;i++) {
        source[i][0]=100+(float)i;source[i][1]=200+(float)i;
        inputs[i]=(OrpheusBuffer){.data=source[i],.format=ORPHEUS_FORMAT_F32,.channels=1,
            .frame_count=2,.frame_capacity=2,.interleaved=true};
        outputs[i]=inputs[i];outputs[i].data=dest[i];
        in[i]=&inputs[i];expected_outputs[i]=&outputs[i];
    }
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext ctx={.inputs=in,.outputs=expected_outputs,.frame_count=2,
        .frame_index=123,.epoch=7,.timeline_flags=1,.trigger_input=UINT32_MAX,.continuation=&continuation};
    const unsigned counts[]={0,1,3};
    for(unsigned a=0;a<3;a++) for(unsigned b=0;b<3;b++) {
        n=counts[a];m=counts[b];values[0].value.i32=(int32_t)n;values[1].value.i32=(int32_t)m;
        assert(api->prepare(instance,&config)==ORPHEUS_OK);
        ctx.input_count=n+2;ctx.output_count=m+2;emissions=0;
        memset(dest,0,sizeof(dest));
        unsigned before=calls;
        assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==before+1 && emissions==m+2);
        assert(dest[0][0]==100 && dest[m+1][1]==201+(float)n);
        for(unsigned i=0;i<m;i++) assert(dest[i+1][0]==(i<n ? 101+(float)i : 100));
        before=calls;
        --ctx.input_count;assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        ctx.input_count+=2;assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);--ctx.input_count;
        --ctx.output_count;assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        ctx.output_count+=2;assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);--ctx.output_count;
        ctx.trigger_input=n+2;assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        ctx.trigger_input=UINT32_MAX;
        OrpheusDelivery delivery={0};ctx.delivery=&delivery;
        assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);ctx.delivery=NULL;
        assert(calls==before);
        ctx.trigger_input=n+1;assert(api->process(instance,&ctx)==ORPHEUS_OK);
        ctx.trigger_input=UINT32_MAX;
    }
    in[1]=NULL;
    assert(api->process(instance,&ctx)==ORPHEUS_OK && dest[1][0]==-99);in[1]=&inputs[1];
    ctx.inputs=NULL;assert(api->process(instance,&ctx)==ORPHEUS_OK && dest[0][0]==-99);ctx.inputs=in;
    null_outputs=true;ctx.outputs=NULL;
    assert(api->process(instance,&ctx)==ORPHEUS_OK);ctx.outputs=expected_outputs;null_outputs=false;
    unsigned before=calls;
    outputs[1].format=ORPHEUS_FORMAT_I16;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);outputs[1].format=ORPHEUS_FORMAT_F32;
    state.n=-1;assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    state.n=4;assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);state.n=3;
    assert(calls==before);
    values[0].value.i32=-1;assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG);
    values[0].value.i32=4;assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG);
    values[0].type=ORPHEUS_VALUE_FLOAT;values[0].value.f32=1.5f;
    assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG);
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
''', encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)
    check_generated(path)


@pytest.mark.parametrize("cpp", [False, True])
@pytest.mark.parametrize("kind", ["audio", "trigger"])
@pytest.mark.parametrize("count_spec", [0, 1, "param:n"])
def test_real_fixed_zero_one_and_zero_bound_groups_are_safe_in_c_and_cpp(package, raw, cpp, kind, count_spec):
    import jsonschema
    expected_count = count_spec if type(count_spec) is int else 0
    raw["interfaces"] = {
        "items": {"type": kind, "direction": "output", "count": count_spec,
                  "optional": True, "drive_port_no": 4294967294},
    }
    if isinstance(count_spec, str):
        raw["interfaces"]["n"] = {
            "type": "int", "default": 0, "range": [0, 0], "update_policy": "restart_required"}
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    path = save(package, raw)
    generate_component(path)
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;
    assert(io->items_count==EXPECTED_COUNT);
    if (!io->items_count) {
        assert(!io->items[0].active);
        assert(test_pilot_emit_items(ctx,&io->items[0])==ORPHEUS_ERR_INVALID_ARG);
        return ORPHEUS_OK;
    }
    assert(io->items[0].active && io->items[0].drive_port_no==UINT32_MAX-1);
    return test_pilot_emit_items(ctx,&io->items[0]);
}
'''.replace("EXPECTED_COUNT", str(expected_count)), encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
static unsigned emissions;
static int emit(void* user,uint32_t drive,const OrpheusDelivery* delivery) {
    (void)user;++emissions;
    assert(drive==UINT32_MAX-1 && delivery->frame_index==17);
    if (delivery->buffer) assert(delivery->buffer->frame_count==1);
    return ORPHEUS_OK;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;
    OrpheusConfig config={.state_block=&state};
    assert(api->create(&instance,&config)==ORPHEUS_OK);
    assert(api->prepare(instance,&config)==ORPHEUS_OK);
    float data=0;
    OrpheusBuffer buffer={.data=&data,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_count=1,.frame_capacity=1,.interleaved=true};
    OrpheusBuffer* outputs[]={TRIGGER ? NULL : &buffer};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext ctx={.outputs=outputs,.output_count=EXPECTED_COUNT,.frame_count=1,
        .frame_index=17,.trigger_input=UINT32_MAX,.continuation=&continuation};
    assert(api->process(instance,&ctx)==ORPHEUS_OK && emissions==EXPECTED_COUNT);
    ctx.trigger_input=0;assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    ctx.trigger_input=UINT32_MAX;ctx.output_count=EXPECTED_COUNT+1;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
'''.replace("TRIGGER", str(int(kind == "trigger"))).replace("EXPECTED_COUNT", str(expected_count)),
        encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


@pytest.mark.parametrize("cpp", [False, True])
def test_real_trigger_groups_select_delivery_by_actual_flat_input_index(package, raw, cpp):
    raw["interfaces"] = {
        "head": {"type": "audio", "direction": "input"},
        "kicks": {"type": "trigger", "direction": "input", "process": True,
                  "count": "param:n", "optional": True},
        "tail": {"type": "audio", "direction": "input"},
        "ticks": {"type": "trigger", "direction": "output", "count": "param:n",
                  "optional": True, "drive_port_no": 100, "drive_divide": 999},
        "done": {"type": "trigger", "direction": "output", "drive_port_no": 200},
        "n": {"type": "int", "default": 0, "range": [0, 3], "update_policy": "restart_required"},
    }
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    assert(io->kicks_count==(unsigned)s->n && io->ticks_count==(unsigned)s->n);
    assert(io->head.input_index==0 && io->tail.input_index==io->kicks_count+1);
    for(unsigned i=0;i<io->kicks_count;i++) {
        assert(io->kicks[i].input_index==i+1);
        assert(io->kicks[i].delivery==(ctx->trigger_input==i+1 ? ctx->delivery : NULL));
    }
    if(io->ticks_count<3)
        assert(test_pilot_emit_ticks(ctx,&io->ticks[io->ticks_count])==ORPHEUS_ERR_INVALID_ARG);
    for(unsigned i=0;i<io->ticks_count;i++) {
        assert(io->ticks[i].drive_port_no==100+i && !io->ticks[i].delivery.buffer);
        assert(test_pilot_emit_ticks(ctx,&io->ticks[i])==ORPHEUS_OK);
        OrpheusBuffer buffer;memset(&buffer,0,sizeof(buffer));
        test_pilot_OutputTrigger wrong=io->ticks[i];wrong.delivery.buffer=&buffer;
        assert(test_pilot_emit_ticks(ctx,&wrong)==ORPHEUS_ERR_INVALID_ARG);
    }
    assert(io->done.active && io->done.drive_port_no==200);
    return test_pilot_emit_done(ctx,&io->done);
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
static unsigned n,emissions;
static int emit(void* user,uint32_t drive,const OrpheusDelivery* delivery) {
    (void)user;
    assert(!delivery->buffer && (drive==200 || (drive>=100 && drive<100+n)));
    assert(delivery->frame_index==18 && delivery->epoch==2 && delivery->timeline_flags==1);
    ++emissions;return ORPHEUS_OK;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;
    const char* keys[]={"n"};
    OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=0};
    OrpheusConfig config={.state_block=&state,.param_ids=keys,.param_values=&value,.param_count=1};
    assert(api->create(&instance,&config)==ORPHEUS_OK);
    float data=0;
    OrpheusBuffer buffer={.data=&data,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_count=1,.frame_capacity=1,.interleaved=true};
    const OrpheusBuffer* inputs[5];
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.frame_count=1,.sample_rate=48000,
        .frame_index=18,.epoch=2,.timeline_flags=1,.continuation=&continuation};
    OrpheusDelivery delivery={.frame_index=18,.epoch=2,.timeline_flags=1};
    const unsigned counts[]={0,1,3};
    for(unsigned k=0;k<3;k++) {
        n=counts[k];value.value.i32=(int32_t)n;
        assert(api->prepare(instance,&config)==ORPHEUS_OK);
        for(unsigned i=0;i<5;i++) inputs[i]=NULL;
        inputs[0]=&buffer;inputs[n+1]=&buffer;
        ctx.input_count=n+2;ctx.output_count=n+1;
        for(unsigned selected=0;selected<n+3;selected++) {
            ctx.trigger_input=selected==n+2 ? UINT32_MAX : selected;
            delivery.buffer=selected==0 || selected==n+1 ? &buffer : NULL;
            ctx.delivery=ctx.trigger_input==UINT32_MAX ? NULL : &delivery;
            emissions=0;
            assert(api->process(instance,&ctx)==ORPHEUS_OK && emissions==n+1);
        }
        if(n) {
            ctx.trigger_input=1;ctx.delivery=&delivery;delivery.buffer=&buffer;
            assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);
            ctx.delivery=NULL;inputs[1]=&buffer;
            assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);inputs[1]=NULL;
        }
    }
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
''', encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


@pytest.mark.parametrize("port_type", ["bulk", "control"])
@pytest.mark.parametrize("elements", [7, 13])
@pytest.mark.parametrize("cpp", [False, True])
@pytest.mark.parametrize("grouped", [False, True])
def test_real_non_audio_payload_size_is_independent_of_task_quantum(
        package, raw, port_type, elements, cpp, grouped):
    raw["interfaces"] = {
        "in": {"type": port_type, "direction": "input", "sample_format": "f32",
               "channels": 2, "block_size": "param:elements", "process": True},
        "out": {"type": port_type, "direction": "output", "sample_format": "f32",
                "channels": 2, "block_size": "in:block_size", "drive_port_no": 1},
        "elements": {"type": "int", "default": elements, "range": [1, 32],
                     "update_policy": "restart_required", "affects_signature": True},
    }
    if grouped:
        raw["interfaces"]["in"]["count"] = 1
        raw["interfaces"]["out"]["count"] = 1
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
unsigned calls;
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    ++calls;
    assert(ctx->frame_count==8 && ctx->valid_frames==8);
    if (IN_VIEW.buffer) assert(IN_VIEW.buffer->frame_count==(unsigned)s->elements);
    if (OUT_VIEW.buffer) assert(OUT_VIEW.buffer->frame_count==(unsigned)s->elements);
    if (OUT_VIEW.samples)
        for(unsigned i=0;i<(unsigned)s->elements*2;i++)
            OUT_VIEW.samples[i]=(IN_VIEW.samples ? IN_VIEW.samples[i] : 0)+5;
    return test_pilot_emit_out(ctx,&OUT_VIEW);
}
'''.replace("IN_VIEW", "io->in[0]" if grouped else "io->in").replace(
        "OUT_VIEW", "io->out[0]" if grouped else "io->out"), encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
extern unsigned calls;
static OrpheusProcessContext* current;
static bool null_output;
static unsigned emissions;
static int emit(void* user,uint32_t drive,const OrpheusDelivery* delivery) {
    (void)user;++emissions;
    assert(drive==1 && current->frame_count==8 && current->valid_frames==8);
    assert(delivery->frame_index==123 && delivery->epoch==9 && delivery->timeline_flags==1);
    if (null_output) assert(!delivery->buffer);
    else assert(delivery->buffer && delivery->buffer->frame_count==ELEMENTS);
    return ORPHEUS_OK;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;
    OrpheusConfig config={.state_block=&state,.block_size=8,.sample_rate=48000};
    assert(api->create(&instance,&config)==ORPHEUS_OK);
    assert(api->prepare(instance,&config)==ORPHEUS_OK);
    float source[ELEMENTS*2],dest[ELEMENTS*2];
    for(unsigned i=0;i<ELEMENTS*2;i++) source[i]=(float)i+1;
    memset(dest,0,sizeof(dest));
    OrpheusBuffer input={.data=source,.format=ORPHEUS_FORMAT_F32,.channels=2,
        .frame_count=ELEMENTS,.frame_capacity=ELEMENTS,.interleaved=true};
    OrpheusBuffer output=input;output.data=dest;output.frame_count=0;
    const OrpheusBuffer* inputs[]={&input};
    OrpheusBuffer* outputs[]={&output};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusDelivery delivery={.buffer=&input,.frame_index=123,.epoch=9,.timeline_flags=1};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=8,.valid_frames=8,.sample_rate=48000,.frame_index=123,.epoch=9,
        .timeline_flags=1,.trigger_input=0,.delivery=&delivery,.continuation=&continuation};
    current=&ctx;
    assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==1 && emissions==1);
    assert(output.frame_count==ELEMENTS && ctx.frame_count==8 && ctx.valid_frames==8);
    for(unsigned i=0;i<ELEMENTS*2;i++) assert(dest[i]==source[i]+5);
    unsigned before=calls;
    output.frame_count=0;output.frame_capacity=ELEMENTS-1;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    output.frame_capacity=ELEMENTS;output.frame_count=ELEMENTS+1;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);output.frame_count=0;
    input.frame_count=ELEMENTS-1;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    input.frame_count=ELEMENTS+1;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);input.frame_count=ELEMENTS;
    input.format=ORPHEUS_FORMAT_I16;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);input.format=ORPHEUS_FORMAT_F32;
    input.channels=1;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);input.channels=2;
    assert(calls==before && ctx.frame_count==8 && ctx.valid_frames==8);
    ctx.inputs=NULL;ctx.delivery=NULL;ctx.trigger_input=UINT32_MAX;
    assert(api->process(instance,&ctx)==ORPHEUS_OK);
    for(unsigned i=0;i<ELEMENTS*2;i++) assert(dest[i]==5);
    null_output=true;ctx.outputs=NULL;
    assert(api->process(instance,&ctx)==ORPHEUS_OK);
    state.elements=0;before=calls;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
    state.elements=-1;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
'''.replace("ELEMENTS", str(elements)), encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


@pytest.mark.parametrize("elements", [7, 13])
@pytest.mark.parametrize("cpp", [False, True])
def test_real_audio_output_still_uses_context_quantum_and_actual_fragments(package, raw, elements, cpp):
    raw["interfaces"] = {
        "out": {"type": "audio", "direction": "output", "sample_format": "f32",
                "channels": 1, "drive_port_no": 1},
        "elements": {"type": "int", "default": elements, "range": [1, 32],
                     "update_policy": "restart_required", "affects_signature": True},
    }
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
unsigned calls;
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;++calls;
    assert(io->out.buffer->frame_count==ctx->frame_count);
    assert(ctx->valid_frames==ctx->frame_count);
    for(unsigned i=0;i<ctx->frame_count;i++) io->out.samples[i]=(float)i+1;
    return test_pilot_emit_out(ctx,&io->out);
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
extern unsigned calls;
static unsigned quantum;
static int emit(void* user,uint32_t drive,const OrpheusDelivery* delivery) {
    (void)user;assert(drive==1 && delivery->buffer->frame_count==quantum);return ORPHEUS_OK;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;
    OrpheusConfig config={.state_block=&state,.block_size=8};
    assert(api->create(&instance,&config)==ORPHEUS_OK && api->prepare(instance,&config)==ORPHEUS_OK);
    float data[ELEMENTS]={0};
    OrpheusBuffer buffer={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_capacity=ELEMENTS,.interleaved=true};
    OrpheusBuffer* outputs[]={&buffer};OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext ctx={.outputs=outputs,.output_count=1,.frame_count=8,.valid_frames=8,
        .trigger_input=UINT32_MAX,.continuation=&continuation};
    quantum=8;
    if(ELEMENTS<8) assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==0);
    else assert(api->process(instance,&ctx)==ORPHEUS_OK && buffer.frame_count==8 && calls==1);
    ctx.frame_count=3;ctx.valid_frames=3;quantum=3;
    assert(api->process(instance,&ctx)==ORPHEUS_OK && buffer.frame_count==3);
    for(unsigned i=0;i<3;i++) assert(data[i]==(float)i+1);
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
'''.replace("ELEMENTS", str(elements)), encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


@pytest.mark.parametrize("block_size,expected", [
    (7, 7), (None, 8), ("in:block_size", 8), ("task:block_size", 8), ("task:sample_rate", 13),
])
@pytest.mark.parametrize("cpp", [False, True])
def test_real_non_audio_static_inherited_and_task_payload_expressions(package, raw, block_size, expected, cpp):
    raw["interfaces"] = {
        "out": {"type": "debug", "direction": "output", "sample_format": "f32",
                "channels": 1, "drive_port_no": 1},
    }
    if block_size is not None:
        raw["interfaces"]["out"]["block_size"] = block_size
    if block_size == "in:block_size":
        raw["interfaces"]["in"] = {"type": "debug", "direction": "input"}
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
unsigned calls;
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;++calls;
    assert(ctx->frame_count==8 && ctx->valid_frames==8);
    assert(io->out.buffer->frame_count==EXPECTED);
    for(unsigned i=0;i<EXPECTED;i++) io->out.samples[i]=(float)i+1;
    return test_pilot_emit_out(ctx,&io->out);
}
'''.replace("EXPECTED", str(expected)), encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <math.h>
extern unsigned calls;
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;
    OrpheusConfig config={.state_block=&state,.block_size=8,.sample_rate=13};
    assert(api->create(&instance,&config)==ORPHEUS_OK && api->prepare(instance,&config)==ORPHEUS_OK);
    float data[EXPECTED]={0};
    OrpheusBuffer buffer={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_capacity=EXPECTED,.interleaved=true};
    OrpheusBuffer* outputs[]={&buffer};
    OrpheusProcessContext ctx={.outputs=outputs,.output_count=1,.frame_count=8,.valid_frames=8,
        .sample_rate=13,.trigger_input=UINT32_MAX};
    assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==1 && buffer.frame_count==EXPECTED);
    for(unsigned i=0;i<EXPECTED;i++) assert(data[i]==(float)i+1);
    if (RATE_PAYLOAD) {
        const double invalid[]={NAN,INFINITY,-INFINITY,-1,0,13.5,4294967296.0};
        for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
            buffer.frame_count=0;ctx.sample_rate=invalid[i];
            assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==1);
            assert(ctx.frame_count==8 && ctx.valid_frames==8);
        }
    }
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
'''.replace("EXPECTED", str(expected)).replace("RATE_PAYLOAD", str(int(block_size == "task:sample_rate"))),
        encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


@pytest.mark.parametrize("custom_state", [False, True])
@pytest.mark.parametrize("parameters", [False, True])
def test_validate_hook_public_parameter_type_normalization_schema_and_initial_stub(package, raw, custom_state, parameters):
    import jsonschema
    raw["algorithm"] = {"lifecycle": ["validate"]}
    if custom_state:
        raw["algorithm"]["state_type"] = "CustomState"
    if parameters:
        _add_bulk_test_interfaces(raw)
    else:
        raw["interfaces"] = {"tick": {"type": "trigger", "direction": "input"}}
    before = copy.deepcopy(raw)
    normalized = normalize_component_definition(raw)
    assert raw == before and normalized["algorithm"]["lifecycle"] == ["validate"]
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    path = save(package, raw)
    generate_component(path)
    header = (package / "generated" / "component_state.h").read_text(encoding="utf-8")
    shell = (package / "generated" / "component_shell.c").read_text(encoding="utf-8")
    user_source = (package / "user" / "algorithm.c").read_text(encoding="utf-8")
    assert "typedef struct test_pilot_Parameters {" in header
    assert "int test_pilot_validate(const test_pilot_Parameters* parameters);" in header
    assert "typedef struct test_pilot_Parameters {" not in shell
    assert "int test_pilot_validate(const test_pilot_Parameters* parameters)" in user_source
    assert "(void)parameters;\n    return ORPHEUS_OK;" in user_source
    if parameters:
        assert "float weights[3];" in header and "char gains[256];" in header
    state_type = normalized["state_type"]
    (package / "harness.c").write_text(
        '#include "component_state.h"\n#include <assert.h>\nint main(void) {\n'
        '    int (*validate)(const test_pilot_Parameters*)=test_pilot_validate;\n'
        '    test_pilot_Parameters parameters={0};assert(validate(&parameters)==ORPHEUS_OK);\n'
        f'    {state_type} memory;void* state=NULL;OrpheusConfig config={{.state_block=&memory}};\n'
        '    const OrpheusComponentInterface* api=orpheus_get_interface();\n'
        '    assert(api->create(&state,&config)==ORPHEUS_OK && api->prepare(state,&config)==ORPHEUS_OK);\n'
        '    assert(api->destroy(state)==ORPHEUS_OK);return 0;\n}\n',
        encoding="utf-8", newline="\n")
    _compile_component_harness(package)
    check_generated(path)


@pytest.mark.parametrize("lifecycle", [["validate", "validate"], ["Validate"], ["validate", None]])
def test_validate_lifecycle_rejects_duplicates_and_invalid_entries_without_writes(package, raw, lifecycle):
    import jsonschema
    raw["algorithm"] = {"lifecycle": lifecycle}
    path = save(package, raw)
    before = snapshot(package)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("cpp", [False, True])
def test_real_validate_hook_rejects_candidates_before_touching_state_or_mutable_hooks(package, raw, cpp):
    raw["algorithm"] = {"lifecycle": ["validate", "prepare", "destroy", "set_parameter"]}
    raw["interfaces"] = {
        "input_channels": {"type": "int", "default": 2, "range": [1, 8], "update_policy": "restart_required"},
        "input_elements": {"type": "int", "default": 8, "range": [1, 32], "update_policy": "restart_required"},
        "channels": {"type": "int", "default": 2, "range": [1, 8], "update_policy": "restart_required",
                     "affects_signature": True},
        "elements": {"type": "int", "default": 4, "range": [1, 32], "update_policy": "restart_required"},
        "offset": {"type": "int", "default": 1, "range": [0, 128], "update_policy": "block_boundary"},
        "stride": {"type": "int", "default": 1, "range": [1, 32], "update_policy": "block_boundary"},
        "label": {"type": "string", "default": "ready", "capacity": 16},
        "weights": {"type": "float", "count": 2, "default": [0.25, 0.75], "range": [0, 1]},
        "gains": {"type": "string", "default": "", "kind": "bulk", "shape": ["param:channels"],
                  "bulk": {"sample_format": "f32", "capacity": 4, "storage": "algorithm.gains"}},
    }
    generate_component(save(package, raw))
    (package / "user" / "algorithm.h").write_text(
        "#pragma once\ntypedef struct test_pilot_AlgorithmState {\n"
        "    float gains[4];int* resource;unsigned marker;\n} test_pilot_AlgorithmState;\n",
        encoding="utf-8", newline="\n")
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
unsigned validations,prepares,destroys,sets;
bool fail_prepare;
int validation_error;
int test_pilot_validate(const test_pilot_Parameters* parameters) {
    /* 计数仅供测试观测调用顺序；生产 validate 不得改写外部状态。 */
    ++validations;
    if(validation_error) return validation_error;
    const uint64_t last=(uint64_t)parameters->offset+
        ((uint64_t)parameters->channels*parameters->elements-1)*(uint64_t)parameters->stride;
    if(last>=(uint64_t)parameters->input_channels*parameters->input_elements)
        return ORPHEUS_ERR_INVALID_ARG;
    if(parameters->weights[0]+parameters->weights[1]!=1.0f) return ORPHEUS_ERR_INVALID_ARG;
    if(strcmp(parameters->label,"deny")==0) return ORPHEUS_ERR_UNSUPPORTED;
    return ORPHEUS_OK;
}
int test_pilot_prepare(test_pilot_State* state,const OrpheusConfig* config) {
    (void)config;++prepares;
    assert(!state->algorithm.resource);
    state->algorithm.resource=(int*)malloc(sizeof(int));
    if(!state->algorithm.resource) return ORPHEUS_ERR_INVALID_ARG;
    *state->algorithm.resource=40+(int)prepares;
    state->algorithm.marker=100+prepares;
    return fail_prepare ? ORPHEUS_ERR_UNSUPPORTED : ORPHEUS_OK;
}
int test_pilot_destroy(test_pilot_State* state) {
    ++destroys;free(state->algorithm.resource);state->algorithm.resource=NULL;
    return ORPHEUS_OK;
}
int test_pilot_set_parameter(test_pilot_State* state,const char* key,const OrpheusValue* value) {
    ++sets;assert(strcmp(key,"offset")==0 && value->type==ORPHEUS_VALUE_INT);
    assert(state->offset==1 && value->value.i32==2);
    state->algorithm.marker+=10;++*state->algorithm.resource;
    return ORPHEUS_OK;
}
int test_pilot_algorithm(test_pilot_State* state,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)io;(void)ctx;++state->algorithm.marker;return ORPHEUS_OK;
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
extern unsigned validations,prepares,destroys,sets;
extern bool fail_prepare;
extern int validation_error;
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;unsigned char before[sizeof(state)];
    memset(&state,0xA5,sizeof(state));memcpy(before,&state,sizeof(state));
    const char* keys[]={"offset"};
    OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=9};
    OrpheusConfig bad={.state_block=&state,.param_ids=keys,.param_values=&value,.param_count=1};
    void* rejected=&state;
    assert(api->create(&rejected,&bad)==ORPHEUS_ERR_INVALID_ARG && !rejected);
    assert(memcmp(before,&state,sizeof(state))==0 && validations==1 && prepares==0 && destroys==0);
    OrpheusConfig config={.state_block=&state};void* instance=NULL;
    assert(api->create(&instance,&config)==ORPHEUS_OK && validations==2 && !state.algorithm.resource);
    assert(api->prepare(instance,&config)==ORPHEUS_OK && validations==3 && prepares==1 && destroys==0);
    assert(state.algorithm.resource && *state.algorithm.resource==41);
    OrpheusProcessContext ctx={.frame_count=8,.trigger_input=UINT32_MAX};
    assert(api->process(instance,&ctx)==ORPHEUS_OK && validations==3);
    memcpy(before,&state,sizeof(state));
    unsigned v=validations,p=prepares,d=destroys,t=sets;
    rejected=&state;
    assert(api->create(&rejected,&bad)==ORPHEUS_ERR_INVALID_ARG && !rejected);
    assert(api->prepare(instance,&bad)==ORPHEUS_ERR_INVALID_ARG);
    assert(api->set_parameter(instance,"offset",&value)==ORPHEUS_ERR_INVALID_ARG);
    assert(validations==v+3 && prepares==p && destroys==d && sets==t);
    assert(memcmp(before,&state,sizeof(state))==0 && *state.algorithm.resource==41);
    value.value.i32=-1;v=validations;
    assert(api->prepare(instance,&bad)==ORPHEUS_ERR_INVALID_ARG);
    assert(api->set_parameter(instance,"offset",&value)==ORPHEUS_ERR_INVALID_ARG);
    assert(validations==v && memcmp(before,&state,sizeof(state))==0);
    value.type=ORPHEUS_VALUE_FLOAT;value.value.f32=1;
    assert(api->prepare(instance,&bad)==ORPHEUS_ERR_INVALID_ARG);
    assert(api->set_parameter(instance,"offset",&value)==ORPHEUS_ERR_INVALID_ARG && validations==v);
    value.type=ORPHEUS_VALUE_INT;value.value.i32=5;keys[0]="channels";
    assert(api->prepare(instance,&bad)==ORPHEUS_ERR_INVALID_ARG);
    assert(api->set_parameter(instance,"channels",&value)==ORPHEUS_ERR_INVALID_ARG && validations==v);
    assert(memcmp(before,&state,sizeof(state))==0 && prepares==p && destroys==d && sets==t);
    value.type=ORPHEUS_VALUE_STRING;value.value.str="0.5,bad";keys[0]="weights";
    assert(api->prepare(instance,&bad)==ORPHEUS_ERR_INVALID_ARG && validations==v);
    value.value.str="0.25,0.25";
    assert(api->prepare(instance,&bad)==ORPHEUS_ERR_INVALID_ARG && validations==v+1);
    assert(memcmp(before,&state,sizeof(state))==0 && *state.algorithm.resource==41);
    value.value.str="deny";keys[0]="label";v=validations;
    assert(api->prepare(instance,&bad)==ORPHEUS_ERR_UNSUPPORTED);
    assert(api->set_parameter(instance,"label",&value)==ORPHEUS_ERR_UNSUPPORTED);
    assert(validations==v+2 && memcmp(before,&state,sizeof(state))==0 && sets==t);
    value.type=ORPHEUS_VALUE_INT;value.value.i32=2;keys[0]="offset";
    validation_error=ORPHEUS_ERR_UNSUPPORTED;v=validations;
    rejected=&state;
    assert(api->create(&rejected,&bad)==ORPHEUS_ERR_UNSUPPORTED && !rejected);
    assert(api->prepare(instance,&bad)==ORPHEUS_ERR_UNSUPPORTED);
    assert(api->set_parameter(instance,"offset",&value)==ORPHEUS_ERR_UNSUPPORTED);
    assert(validations==v+3 && memcmp(before,&state,sizeof(state))==0 && sets==t);
    assert(prepares==p && destroys==d && *state.algorithm.resource==41);
    validation_error=0;
    assert(api->set_parameter(instance,"offset",&value)==ORPHEUS_OK);
    assert(state.offset==2 && sets==t+1 && *state.algorithm.resource==42);
    value.value.i32=3;keys[0]="stride";memcpy(before,&state,sizeof(state));t=sets;
    assert(api->set_parameter(instance,"stride",&value)==ORPHEUS_ERR_INVALID_ARG);
    assert(memcmp(before,&state,sizeof(state))==0 && *state.algorithm.resource==42 && sets==t);
    const char* good_keys[]={"offset","label","weights"};
    OrpheusValue good_values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=0},
        {.type=ORPHEUS_VALUE_STRING,.value.str="new"},
        {.type=ORPHEUS_VALUE_STRING,.value.str="0.5,0.5"}};
    OrpheusConfig good={.state_block=&state,.param_ids=good_keys,.param_values=good_values,.param_count=3};
    assert(api->prepare(instance,&good)==ORPHEUS_OK && prepares==p+1 && destroys==d+1);
    assert(state.offset==0 && strcmp(state.label,"new")==0 && state.weights[0]==0.5f);
    assert(*state.algorithm.resource==42 && state.algorithm.marker==102);
    fail_prepare=true;
    assert(api->prepare(instance,&config)==ORPHEUS_ERR_UNSUPPORTED);
    assert(prepares==p+2 && destroys==d+3 && !state.algorithm.resource && state.algorithm.marker==0);
    assert(state.offset==1 && strcmp(state.label,"ready")==0 && state._orpheus_alive);
    assert(!state._orpheus_cleanup_pending);
    fail_prepare=false;
    assert(api->prepare(instance,&config)==ORPHEUS_OK && prepares==p+3);
    assert(api->destroy(instance)==ORPHEUS_OK && destroys==d+4);
    assert(api->destroy(instance)==ORPHEUS_OK && destroys==d+4);
    return 0;
}
''', encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


def _add_input_alternative_test_interfaces(raw, port_type="bulk"):
    raw["interfaces"] = {
        name: {"type": port_type, "direction": "input", "sample_format": "f32",
               "channels": 1, "block_size": 2}
        for name in ("a", "b", "c", "d")
    }
    raw["interfaces"]["out"] = {
        "type": "audio", "direction": "output", "channels": 1, "block_size": 2, "drive_port_no": 1}
    raw["input_alternatives"] = [["a", "b"]]


def test_input_alternatives_preserve_single_source_projection_and_generation_freshness(package, raw):
    import jsonschema
    _add_input_alternative_test_interfaces(raw)
    raw.pop("input_alternatives")
    path = save(package, raw)
    generate_component(path)
    baseline = {p.name: p.read_bytes() for p in (package / "generated").iterdir()
                if p.name != "component_codegen.json"}
    raw["input_alternatives"] = []
    save(package, raw)
    generate_component(path)
    assert baseline == {p.name: p.read_bytes() for p in (package / "generated").iterdir()
                        if p.name != "component_codegen.json"}
    raw["input_alternatives"] = [["a", "b"], ["c", "d"]]
    original = copy.deepcopy(raw)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    normalized = normalize_component_definition(raw)
    assert raw == original and normalized["input_alternatives"] == original["input_alternatives"]
    normalized["input_alternatives"][0][0] = "changed"
    assert raw == original
    save(package, raw)
    with pytest.raises(ComponentDefinitionError):
        check_generated(path)
    generate_component(path)
    check_generated(path)
    shell = (package / "generated" / "component_shell.c").read_text(encoding="utf-8")
    assert "(uint32_t)(io.a.buffer != NULL) + (uint32_t)(io.b.buffer != NULL)" in shell
    assert "(uint32_t)(io.c.buffer != NULL) + (uint32_t)(io.d.buffer != NULL)" in shell
    generated = snapshot(package)
    generate_component(path)
    assert snapshot(package) == generated


@pytest.mark.parametrize("groups", [
    None, True, 1, "a,b", {"a": "b"}, [None], [1], ["a,b"], [{}], [[]],
    [["a"]], [["a", "a"]], [["a", None]], [["a", 1]], [["a", []]], [["a", {}]],
    [["a", "b"], ["a", "b"]], [["a", "b"], ["b", "a"]], [["a", "b"], ["b", "c"]],
    [["a", "missing"]], [["a", "out"]],
])
@pytest.mark.parametrize("existing", [False, True])
def test_invalid_input_alternatives_never_write_component_package(package, raw, groups, existing):
    _add_input_alternative_test_interfaces(raw)
    if existing:
        generate_component(save(package, raw))
    raw["input_alternatives"] = groups
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError) as error:
        generate_component(path)
    assert error.value.diagnostic["location"].startswith("input_alternatives")
    assert snapshot(package) == before


@pytest.mark.parametrize("groups", [("a", "b"), [("a", "b")]])
def test_input_alternative_python_projection_rejects_tuple_containers(raw, groups):
    _add_input_alternative_test_interfaces(raw)
    raw["input_alternatives"] = groups
    with pytest.raises(ComponentDefinitionError):
        normalize_component_definition(raw)


@pytest.mark.parametrize("target", [
    {"type": "int", "default": 1},
    {"type": "float", "default": 1},
    {"type": "trigger", "direction": "input", "process": True},
    {"type": "audio", "direction": "output"},
    {"type": "bulk", "direction": "input", "count": 1},
    {"type": "control", "direction": "input", "count": 0, "optional": True},
    {"type": "debug", "direction": "input", "count": "param:n", "optional": True},
])
def test_input_alternatives_require_single_countless_input_data_ports(package, raw, target):
    _add_input_alternative_test_interfaces(raw)
    raw["interfaces"]["b"] = target
    raw["interfaces"]["n"] = {
        "type": "int", "default": 1, "range": [0, 3], "update_policy": "restart_required"}
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError, match="单个输入数据接口"):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("groups", [
    None, {}, "a,b", [None], [["a"]], [["a", "a"]], [["a", 1]], [["a", "b"], ["a", "b"]],
])
def test_input_alternative_container_shape_is_strict_in_json_schema(raw, groups):
    import jsonschema
    _add_input_alternative_test_interfaces(raw)
    raw["input_alternatives"] = groups
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)


def test_input_alternative_independent_clis_support_json_and_readonly_errors(package, raw):
    _add_input_alternative_test_interfaces(raw)
    path = save(package, raw)
    scripts = [ROOT / "scripts" / name for name in ("validate_component.py", "generate_component.py")]
    before = snapshot(package)
    for index, script in enumerate(scripts):
        result = subprocess.run([str(PYTHON), str(script), str(path), "--format", "json"],
                                cwd=package, capture_output=True, encoding="utf-8")
        assert result.returncode == 0, result.stderr
        assert json.loads(result.stdout)["ok"] is True
        if index == 0:
            assert snapshot(package) == before
    check_generated(path)
    raw["input_alternatives"] = [["a", "b"], ["b", "c"]]
    save(package, raw)
    before = snapshot(package)
    for script in scripts:
        for output in ("json", "text"):
            result = subprocess.run([str(PYTHON), str(script), str(path), "--format", output],
                                    cwd=package, capture_output=True, encoding="utf-8")
            assert result.returncode == 1, result.stderr
            if output == "json":
                diagnostic = json.loads(result.stdout)["diagnostics"][0]
                assert diagnostic["location"] == "input_alternatives[1][0]"
            else:
                assert "重复、交叠" in result.stdout
    assert snapshot(package) == before


@pytest.mark.parametrize("port_type", ["audio", "bulk", "control", "debug"])
@pytest.mark.parametrize("cpp", [False, True])
@pytest.mark.parametrize("two_groups", [False, True])
def test_real_input_alternatives_validate_buffer_presence_before_algorithm_and_skip_zero_quantum(
        package, raw, port_type, cpp, two_groups):
    _add_input_alternative_test_interfaces(raw, port_type)
    raw["input_alternatives"] = [["a", "b"], ["c", "d"]] if two_groups else [["a", "b", "c"]]
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
unsigned calls,selected;
int test_pilot_algorithm(test_pilot_State* state,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)state;++calls;
    assert(ctx->frame_count==2);
    selected=(io->a.buffer ? 1u : 0u)|(io->b.buffer ? 2u : 0u)|
        (io->c.buffer ? 4u : 0u)|(io->d.buffer ? 8u : 0u);
    if(io->out.samples) io->out.samples[0]=(float)selected;
    return ORPHEUS_OK;
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
extern unsigned calls,selected;
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;
    OrpheusConfig config={.state_block=&state,.block_size=2};
    assert(api->create(&instance,&config)==ORPHEUS_OK && api->prepare(instance,&config)==ORPHEUS_OK);
    float data[2]={1,2},dest[2]={0};
    OrpheusBuffer input={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_count=2,.frame_capacity=2,.interleaved=true};
    OrpheusBuffer output=input;output.data=dest;
    const OrpheusBuffer* inputs[4];
    OrpheusBuffer* outputs[]={&output};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=4,.output_count=1,
        .frame_count=2,.valid_frames=2,.trigger_input=UINT32_MAX};
    for(unsigned null_out=0;null_out<2;null_out++) {
        ctx.outputs=null_out ? NULL : outputs;
        for(unsigned mask=0;mask<16;mask++) {
            for(unsigned i=0;i<4;i++) inputs[i]=(mask&(1u<<i)) ? &input : NULL;
            bool valid=TWO_GROUPS ? (((mask&3)==1 || (mask&3)==2) && ((mask&12)==4 || (mask&12)==8))
                : ((mask&7)==1 || (mask&7)==2 || (mask&7)==4);
            unsigned before=calls;
            int result=api->process(instance,&ctx);
            if(valid) {
                assert(result==ORPHEUS_OK && calls==before+1 && selected==mask);
                if(!null_out) assert(dest[0]==(float)mask);
            } else assert(result==ORPHEUS_ERR_INVALID_ARG && calls==before);
        }
    }
    ctx.inputs=NULL;ctx.outputs=NULL;unsigned missing_before=calls;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==missing_before);
    ctx.inputs=inputs;ctx.outputs=outputs;inputs[0]=&input;inputs[1]=NULL;
    inputs[2]=TWO_GROUPS ? &input : NULL;inputs[3]=NULL;
    unsigned before=calls;
    input.format=ORPHEUS_FORMAT_I16;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
    input.format=ORPHEUS_FORMAT_F32;
    if(AUDIO_INPUT) {
        OrpheusBuffer empty=input;empty.data=NULL;empty.frame_count=0;empty.frame_capacity=0;
        inputs[0]=&empty;
        assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
        inputs[0]=&input;
    }
    ctx.frame_count=0;ctx.valid_frames=0;before=calls;output.frame_count=1;
    for(unsigned mask=0;mask<16;mask++) {
        for(unsigned i=0;i<4;i++) inputs[i]=(mask&(1u<<i)) ? &input : NULL;
        assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==before);
        assert(output.frame_count==1);
    }
    ctx.inputs=NULL;ctx.outputs=NULL;
    assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==before);
    assert(api->process(NULL,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    assert(api->process(instance,NULL)==ORPHEUS_ERR_INVALID_ARG);
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
'''.replace("TWO_GROUPS", str(int(two_groups))).replace("AUDIO_INPUT", str(int(port_type == "audio"))),
        encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


def _add_f32_max_test_interfaces(raw):
    maximum = struct.unpack("!f", bytes.fromhex("7f7fffff"))[0]
    raw["interfaces"] = {
        "epsilon": {"type": "float", "default": 2.22044605e-16, "range": [0, maximum]},
        "limit": {"type": "float", "default": -maximum, "range": [-maximum, maximum]},
        "weights": {"type": "float", "count": 2, "default": [maximum, -maximum],
                    "range": [-maximum, maximum], "update_policy": "restart_required"},
        "gains": {"type": "string", "default": "", "kind": "bulk",
                  "bulk": {"sample_format": "f32", "capacity": 2, "storage": "algorithm.coefs",
                           "range": [-maximum, maximum], "double_bank": True}},
    }
    return maximum


def test_f32_max_matches_ieee_bits_and_exact_endpoints_work_in_schema_cli_and_initializer(package, raw):
    import jsonschema
    maximum = _add_f32_max_test_interfaces(raw)
    assert F32_MAX == maximum == (2 ** 24 - 1) * 2 ** 104
    assert 3.402823466e38 < maximum
    assert struct.pack("!f", F32_MAX) == bytes.fromhex("7f7fffff")
    assert encode_array_initializer({"type": "float", "count": 2}, [maximum, -maximum]) == (
        f"{maximum!r},{-maximum!r}")
    assert encode_array_initializer({"type": "float", "count": 1}, [int(maximum)]) == repr(maximum)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    path = save(package, raw)
    before = snapshot(package)
    loaded = load_component_definition(path)
    assert loaded["interfaces"]["epsilon"]["range"] == [0, maximum]
    assert snapshot(package) == before
    for script in ("validate_component.py", "generate_component.py"):
        result = subprocess.run([str(PYTHON), str(ROOT / "scripts" / script), str(path), "--format", "json"],
                                cwd=package, capture_output=True, encoding="utf-8")
        assert result.returncode == 0, result.stderr + result.stdout
        assert json.loads(result.stdout)["ok"] is True
    check_generated(path)


@pytest.mark.parametrize("target", ["scalar_default", "scalar_range", "array_default", "array_range", "bulk_range"])
@pytest.mark.parametrize("sign", [-1, 1])
@pytest.mark.parametrize("boundary", ["next_double", "next_integer"])
def test_f32_values_just_outside_max_are_rejected_without_writes(package, raw, target, sign, boundary):
    import jsonschema
    maximum = _add_f32_max_test_interfaces(raw)
    outside = sign * (math.nextafter(maximum, math.inf) if boundary == "next_double" else int(maximum) + 1)
    assert abs(outside) > maximum
    bounds = [-maximum, outside] if sign > 0 else [outside, maximum]
    if target == "scalar_default":
        raw["interfaces"]["limit"]["default"] = outside
    elif target == "scalar_range":
        raw["interfaces"]["limit"]["range"] = bounds
    elif target == "array_default":
        raw["interfaces"]["weights"]["default"][0] = outside
    elif target == "array_range":
        raw["interfaces"]["weights"]["range"] = bounds
    else:
        raw["interfaces"]["gains"]["bulk"]["range"] = bounds
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    with pytest.raises(ComponentDefinitionError):
        encode_array_initializer({"type": "float", "count": 1}, [outside])
    assert snapshot(package) == before


@pytest.mark.parametrize("cpp", [False, True])
def test_real_f32_exact_max_defaults_setters_csv_and_resource_bounds(package, raw, cpp):
    maximum = _add_f32_max_test_interfaces(raw)
    generate_component(save(package, raw))
    good_csv = f"{maximum!r},{-maximum!r}"
    outside = math.nextafter(maximum, math.inf)
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <string.h>
static unsigned registrations;
static OrpheusSlotId add(void* user,const OrpheusSlotInfo* slot) {
    (void)user;++registrations;
    assert(slot->type==ORPHEUS_VALUE_FLOAT && slot->max_f32==FLT_MAX);
    if(strcmp(slot->key,"epsilon")==0) assert(slot->min_f32==0);
    else assert(slot->min_f32==-FLT_MAX);
    if(slot->kind==ORPHEUS_SLOT_BULK) assert(slot->count==2);
    return registrations;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    const OrpheusComponentDescriptor* descriptor=api->get_descriptor();
    assert(descriptor->params[0].max_f32==FLT_MAX);
    assert(descriptor->params[1].min_f32==-FLT_MAX && descriptor->params[1].max_f32==FLT_MAX);
    test_pilot_State state;void* instance=NULL;
    OrpheusConfig config={.state_block=&state};
    assert(api->create(&instance,&config)==ORPHEUS_OK && api->prepare(instance,&config)==ORPHEUS_OK);
    assert(state.epsilon>0 && isfinite(state.epsilon));
    assert(state.limit==-FLT_MAX && state.weights[0]==FLT_MAX && state.weights[1]==-FLT_MAX);
    OrpheusRegistry registry={.add=add};
    assert(api->register_slots(instance,&registry)==ORPHEUS_OK && registrations==4);
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=FLT_MAX},readback;
    assert(api->set_parameter(instance,"epsilon",&value)==ORPHEUS_OK && state.epsilon==FLT_MAX);
    assert(api->get_parameter(instance,"epsilon",&readback)==ORPHEUS_OK && readback.value.f32==FLT_MAX);
    value.value.f32=-FLT_MAX;
    assert(api->set_parameter(instance,"limit",&value)==ORPHEUS_OK && state.limit==-FLT_MAX);
    unsigned char before[sizeof(state)];memcpy(before,&state,sizeof(state));
    const float adjacent=nextafterf(FLT_MAX,INFINITY);
    assert(isinf(adjacent));
    const float invalid[]={adjacent,-adjacent,NAN,-FLT_MAX};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        value.value.f32=invalid[i];
        assert(api->set_parameter(instance,"epsilon",&value)==ORPHEUS_ERR_INVALID_ARG);
        assert(memcmp(before,&state,sizeof(state))==0);
    }
    const char* keys[]={"weights"};
    value.type=ORPHEUS_VALUE_STRING;value.value.str=GOOD_CSV;
    config.param_ids=keys;config.param_values=&value;config.param_count=1;
    assert(api->prepare(instance,&config)==ORPHEUS_OK);
    assert(state.weights[0]==FLT_MAX && state.weights[1]==-FLT_MAX);
    memcpy(before,&state,sizeof(state));
    const char* invalid_csv[]={POSITIVE_OUTSIDE_CSV,NEGATIVE_OUTSIDE_CSV};
    for(unsigned i=0;i<2;i++) {
        value.value.str=invalid_csv[i];
        assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG);
        assert(memcmp(before,&state,sizeof(state))==0);
    }
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
'''.replace("GOOD_CSV", json.dumps(good_csv)).replace(
        "POSITIVE_OUTSIDE_CSV", json.dumps(f"{outside!r},0")).replace(
        "NEGATIVE_OUTSIDE_CSV", json.dumps(f"{-outside!r},0")), encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


def _add_unready_bulk_input(raw):
    raw["interfaces"] = {
        "in": {"type": "bulk", "direction": "input", "sample_format": "f32",
               "channels": 2, "block_size": "param:elements", "timing": "latest",
               "allow_unready": True},
        "elements": {"type": "int", "default": 7, "range": [1, 16],
                     "update_policy": "restart_required", "affects_signature": True},
    }


@pytest.mark.parametrize("process", [None, False])
def test_allow_unready_schema_normalization_and_independent_cli(package, raw, process):
    import jsonschema
    _add_unready_bulk_input(raw)
    if process is not None:
        raw["interfaces"]["in"]["process"] = process
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    path = save(package, raw)
    before = snapshot(package)
    normalized = normalize_component_definition(load_component_definition(path))
    assert normalized["ports"][0]["allow_unready"] is True
    assert normalized["ports"][0]["process"] is False
    normalized["ports"][0]["allow_unready"] = False
    assert raw["interfaces"]["in"]["allow_unready"] is True
    assert snapshot(package) == before
    for script in ("validate_component.py", "generate_component.py"):
        result = subprocess.run([str(PYTHON), str(ROOT / "scripts" / script), str(path), "--format", "json"],
                                cwd=package, capture_output=True, encoding="utf-8")
        assert result.returncode == 0, result.stderr + result.stdout
        assert json.loads(result.stdout)["ok"] is True
        if script == "validate_component.py":
            assert snapshot(package) == before
    check_generated(path)
    raw["interfaces"]["in"]["allow_unready"] = False
    save(package, raw)
    with pytest.raises(ComponentDefinitionError):
        check_generated(path)
    generate_component(path)
    check_generated(path)
    strict_shell = (package / "generated" / "component_shell.c").read_bytes()
    del raw["interfaces"]["in"]["allow_unready"]
    generate_component(save(package, raw))
    assert (package / "generated" / "component_shell.c").read_bytes() == strict_shell


@pytest.mark.parametrize("value", [None, 0, 1, "true", [], {}])
def test_allow_unready_requires_boolean_without_writes(package, raw, value):
    import jsonschema
    _add_unready_bulk_input(raw)
    raw["interfaces"]["in"]["allow_unready"] = value
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError, match="布尔"):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("value", [False, True])
@pytest.mark.parametrize("overrides", [
    {"direction": "output"},
    {"type": "audio"},
    {"type": "trigger"},
    {"type": "control"},
    {"type": "debug"},
    {"type": "float", "default": 0.0},
    {"process": True},
    {"timing": "sync"},
    {"timing": None},
])
def test_allow_unready_rejects_non_latest_passive_bulk_inputs(package, raw, overrides, value):
    import jsonschema
    _add_unready_bulk_input(raw)
    port = raw["interfaces"]["in"]
    port.update(overrides)
    port["allow_unready"] = value
    if port.get("timing") is None:
        del port["timing"]
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    result = subprocess.run(
        [str(PYTHON), str(ROOT / "scripts" / "generate_component.py"), str(path), "--format", "json"],
        cwd=package, capture_output=True, encoding="utf-8")
    assert result.returncode == 1
    assert json.loads(result.stdout)["ok"] is False
    assert snapshot(package) == before


@pytest.mark.parametrize("allow_unready", [None, False, True])
@pytest.mark.parametrize("elements", [7, 13])
@pytest.mark.parametrize("grouped", [False, True])
@pytest.mark.parametrize("cpp", [False, True])
def test_real_allow_unready_bulk_keeps_header_capacity_and_ready_validation(
        package, raw, allow_unready, elements, grouped, cpp):
    _add_unready_bulk_input(raw)
    raw["interfaces"]["elements"]["default"] = elements
    if allow_unready is None:
        del raw["interfaces"]["in"]["allow_unready"]
    else:
        raw["interfaces"]["in"]["allow_unready"] = allow_unready
    if grouped:
        raw["interfaces"]["in"]["count"] = 2
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
unsigned calls;
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    ++calls;
    assert(ctx->frame_count==8 && ctx->valid_frames==8);
    if (!IN_VIEW.buffer) { assert(!IN_VIEW.samples);return ORPHEUS_OK; }
    assert(IN_VIEW.samples && IN_VIEW.input_index==0);
    assert(IN_VIEW.buffer->frame_capacity>=(unsigned)s->elements);
    assert(IN_VIEW.buffer->frame_count==0 || IN_VIEW.buffer->frame_count==(unsigned)s->elements);
    if (IN_VIEW.buffer->frame_count)
        for(unsigned i=0;i<2*(unsigned)s->elements;i++) assert(IN_VIEW.samples[i]==(float)i+1);
    return ORPHEUS_OK;
}
'''.replace("IN_VIEW", "io->in[0]" if grouped else "io->in"), encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
extern unsigned calls;
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;
    OrpheusConfig config={.state_block=&state,.block_size=8,.sample_rate=48000};
    assert(api->create(&instance,&config)==ORPHEUS_OK);
    assert(api->prepare(instance,&config)==ORPHEUS_OK);
    float samples[2*(ELEMENTS+1)];
    for(unsigned i=0;i<2*ELEMENTS;i++) samples[i]=(float)i+1;
    OrpheusBuffer input={.data=samples,.format=ORPHEUS_FORMAT_F32,.channels=2,
        .frame_count=ELEMENTS,.frame_capacity=ELEMENTS,.interleaved=true};
    const OrpheusBuffer original=input;
    const OrpheusBuffer* inputs[]={&input,&input};
    OrpheusProcessContext ctx={.inputs=inputs,.input_count=INPUT_COUNT,
        .frame_count=8,.valid_frames=8,.sample_rate=48000,.trigger_input=UINT32_MAX};
    assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==1);
    input.frame_count=0;
    unsigned before=calls;
    assert(api->process(instance,&ctx)==ZERO_RESULT);
    assert(calls==before+ZERO_CALLS && input.frame_count==0 && input.frame_capacity==ELEMENTS);
#define REJECT_BAD(change) do { \
    input=original;change;before=calls; \
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before); \
} while(0)
    REJECT_BAD(input.frame_count=ELEMENTS-1);
    REJECT_BAD(input.frame_count=1);
    REJECT_BAD(input.frame_count=ELEMENTS+1);
    REJECT_BAD(input.frame_count=0;input.frame_capacity=ELEMENTS-1);
    REJECT_BAD(input.frame_count=0;input.frame_capacity=0);
    REJECT_BAD(input.frame_count=0;input.data=NULL);
    REJECT_BAD(input.data=NULL);
    REJECT_BAD(input.frame_count=0;input.channels=1);
    REJECT_BAD(input.frame_count=0;input.format=ORPHEUS_FORMAT_I16);
    REJECT_BAD(input.frame_count=0;input.interleaved=false);
    REJECT_BAD(input.frame_count=0;input.data=(unsigned char*)samples+1);
    input=original;input.frame_capacity=ELEMENTS+1;
    assert(api->process(instance,&ctx)==ORPHEUS_OK && input.frame_count==ELEMENTS);
    ctx.inputs=NULL;
    assert(api->process(instance,&ctx)==ORPHEUS_OK);
    assert(ctx.frame_count==8 && ctx.valid_frames==8);
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
'''.replace("ELEMENTS", str(elements)).replace("INPUT_COUNT", "2" if grouped else "1").replace(
        "ZERO_RESULT", "ORPHEUS_OK" if allow_unready else "ORPHEUS_ERR_INVALID_ARG").replace(
        "ZERO_CALLS", "1" if allow_unready else "0"), encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


def _add_spectral_payload_interfaces(raw):
    raw["interfaces"] = {
        "in": {"type": "bulk", "direction": "input", "sample_format": "f32",
               "channels": 1, "block_size": "param:bins*2*param:batches", "process": True},
        "out": {"type": "audio", "direction": "output", "sample_format": "f32",
                "channels": 1, "block_size": "param:fft_size*param:batches", "drive_port_no": 1},
        "spectrum": {"type": "bulk", "direction": "output", "sample_format": "f32",
                     "channels": 1, "block_size": "out:block_size", "layout": "halfcomplex",
                     "frame_size": "param:fft_size", "drive_port_no": 2},
        "bins": {"type": "int", "default": 5, "range": [2, 32769],
                 "update_policy": "restart_required", "affects_signature": True},
        "fft_size": {"type": "int", "default": 8, "range": [2, 65536],
                     "update_policy": "restart_required", "affects_signature": True},
        "batches": {"type": "int", "default": 2, "range": [1, 128],
                    "update_policy": "restart_required", "affects_signature": True},
    }


@pytest.mark.parametrize("expression", [
    "param:bins*2*param:batches", "param:fft_size*param:batches",
    "param:fft_size*2", "2*param:fft_size", "2*3", "3*1431655765",
])
def test_payload_product_schema_cli_projection_and_large_legal_upper_bound(package, raw, expression):
    import jsonschema
    _add_spectral_payload_interfaces(raw)
    raw["interfaces"]["in"]["block_size"] = expression
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    path = save(package, raw)
    loaded = load_component_definition(path)
    projected = normalize_component_definition(loaded)
    assert projected["ports"][0]["block_size"] == expression
    assert projected["ports"][2]["layout"] == "halfcomplex"
    assert projected["ports"][2]["frame_size"] == "param:fft_size"
    assert projected["parameters"][1]["range"] == [2, 65536]
    before = snapshot(package)
    for script in ("validate_component.py", "generate_component.py"):
        result = subprocess.run([str(PYTHON), str(ROOT / "scripts" / script), str(path), "--format", "json"],
                                cwd=package, capture_output=True, encoding="utf-8")
        assert result.returncode == 0, result.stderr + result.stdout
        assert json.loads(result.stdout)["ok"] is True
        if script == "validate_component.py":
            assert snapshot(package) == before
    check_generated(path)


@pytest.mark.parametrize("expression", [
    "param:bins**2", "*param:bins", "param:bins*", "param:bins*0",
    "param:bins*-2", "param:bins*1.5", "param:bins//2", "param:bins+2",
    "(param:bins)*2", "param:bins * 2", "param:bins*True",
    "param:bins*in:block_size", "param:bins*task:block_size", "param:bins*02",
    "param:bins*__import__('os')", "param:bins*2\n", "param:bins*２",
])
def test_payload_product_rejects_non_dsl_syntax_in_schema_cli_without_writes(package, raw, expression):
    import jsonschema
    _add_spectral_payload_interfaces(raw)
    raw["interfaces"]["in"]["block_size"] = expression
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    result = subprocess.run(
        [str(PYTHON), str(ROOT / "scripts" / "generate_component.py"), str(path), "--format", "json"],
        cwd=package, capture_output=True, encoding="utf-8")
    assert result.returncode == 1
    assert json.loads(result.stdout)["ok"] is False
    assert snapshot(package) == before


@pytest.mark.parametrize("case", [
    "unknown", "float", "bool", "array", "runtime", "non_signature", "zero_range",
    "negative_range", "range_overflow", "constant_overflow", "long_constant",
    "channels_product", "sample_rate_product", "frame_size_product",
])
def test_payload_product_rejects_bad_references_types_ranges_and_overflows(package, raw, case):
    _add_spectral_payload_interfaces(raw)
    port, parameter = raw["interfaces"]["in"], raw["interfaces"]["bins"]
    if case == "unknown":
        port["block_size"] = "param:missing*2"
    elif case in {"float", "bool"}:
        parameter.update(type=case, default=2.0 if case == "float" else True)
        parameter.pop("range")
    elif case == "array":
        parameter.update(count=1, default=[5], affects_signature=False)
    elif case == "runtime":
        parameter.update(update_policy="block_boundary", affects_signature=False)
    elif case == "non_signature":
        parameter["affects_signature"] = False
    elif case in {"zero_range", "negative_range"}:
        parameter["range"][0] = 0 if case == "zero_range" else -1
    elif case == "range_overflow":
        parameter["range"][1] = 2147483647
    elif case == "constant_overflow":
        port["block_size"] = "65536*65536"
    elif case == "long_constant":
        port["block_size"] = "1*" + "9" * 5000
    else:
        field = {"channels_product": "channels", "sample_rate_product": "sample_rate",
                 "frame_size_product": "frame_size"}[case]
        port[field] = "param:bins*2"
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("case", [
    "audio", "control", "debug", "trigger", "i16", "i32", "no_frame", "zero_frame",
    "no_payload", "unknown_frame", "runtime_frame", "float_frame", "array_frame",
    "fraction_frame", "negative_frame", "frame_overflow", "bad_default_divisibility",
])
def test_halfcomplex_layout_rejects_invalid_combinations_without_writes(package, raw, case):
    import jsonschema
    _add_spectral_payload_interfaces(raw)
    port = raw["interfaces"]["spectrum"]
    if case in {"audio", "control", "debug", "trigger"}:
        port["type"] = case
    elif case in {"i16", "i32"}:
        port["sample_format"] = case
    elif case == "no_frame":
        del port["frame_size"]
    elif case == "zero_frame":
        port["frame_size"] = 0
    elif case == "no_payload":
        del port["block_size"]
    elif case == "unknown_frame":
        port["frame_size"] = "param:missing"
    elif case == "runtime_frame":
        raw["interfaces"]["fft_size"].update(update_policy="block_boundary", affects_signature=False)
    elif case == "float_frame":
        raw["interfaces"]["fft_size"].update(type="float", default=8.0)
    elif case == "array_frame":
        raw["interfaces"]["fft_size"].update(count=1, default=[8], affects_signature=False)
    elif case == "fraction_frame":
        port["frame_size"] = 2.5
    elif case == "negative_frame":
        port["frame_size"] = -1
    elif case == "frame_overflow":
        port["frame_size"] = 4294967296
    else:
        port["block_size"] = 15
    if case in {"audio", "control", "debug", "trigger", "i16", "i32", "no_frame",
                "zero_frame", "no_payload", "fraction_frame", "negative_frame", "frame_overflow"}:
        schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
        with pytest.raises(jsonschema.ValidationError):
            jsonschema.validate(raw, schema)
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("cpp", [False, True])
@pytest.mark.parametrize("inherited_audio", [False, True])
@pytest.mark.parametrize("audio_input", [False, True])
def test_real_spectral_product_payload_and_halfcomplex_frames_preserve_task_quantum(
        package, raw, cpp, inherited_audio, audio_input):
    _add_spectral_payload_interfaces(raw)
    if audio_input:
        raw["interfaces"]["in"]["type"] = "audio"
    if inherited_audio:
        raw["interfaces"]["out"]["block_size"] = "spectrum:block_size"
        raw["interfaces"]["spectrum"]["block_size"] = "param:fft_size*param:batches"
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
unsigned calls;
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    ++calls;
    assert(ctx->frame_count==8 && ctx->valid_frames==8);
    uint32_t source=(uint32_t)s->bins*2u*(uint32_t)s->batches;
    uint32_t frames=(uint32_t)s->fft_size*(uint32_t)s->batches;
    if(io->in.buffer) {
        assert(io->in.buffer->frame_count==source);
        assert(io->in.samples[0]==17 && io->in.samples[source-1]==19);
    }
    if(io->out.buffer) {
        assert(io->out.buffer->frame_count==frames);
        io->out.samples[0]=23;io->out.samples[frames-1]=29;
    }
    if(io->spectrum.buffer) {
        assert(io->spectrum.buffer->frame_count==frames);
        io->spectrum.samples[0]=31;io->spectrum.samples[frames-1]=37;
    }
    int result=test_pilot_emit_out(ctx,&io->out);
    return result==ORPHEUS_OK ? test_pilot_emit_spectrum(ctx,&io->spectrum) : result;
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
extern unsigned calls;
static uint32_t expected_frames;
static unsigned emissions;
static int emit(void* user,uint32_t drive,const OrpheusDelivery* delivery) {
    (void)user;++emissions;
    assert(drive==1 || drive==2);
    assert(delivery->frame_index==123 && delivery->epoch==7 && delivery->timeline_flags==1);
    if(delivery->buffer) assert(delivery->buffer->frame_count==expected_frames);
    return ORPHEUS_OK;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;
    OrpheusConfig config={.state_block=&state,.block_size=8,.sample_rate=48000};
    assert(api->create(&instance,&config)==ORPHEUS_OK && api->prepare(instance,&config)==ORPHEUS_OK);
    const int fft[]={2,8,65536},batches[]={1,2,128};
    for(unsigned iteration=0;iteration<3;iteration++) {
        const char* keys[]={"fft_size","bins","batches"};
        OrpheusValue values[]={
            {.type=ORPHEUS_VALUE_INT,.value.i32=fft[iteration]},
            {.type=ORPHEUS_VALUE_INT,.value.i32=fft[iteration]/2+1},
            {.type=ORPHEUS_VALUE_INT,.value.i32=batches[iteration]}};
        config.param_ids=keys;config.param_values=values;config.param_count=3;
        assert(api->prepare(instance,&config)==ORPHEUS_OK);
        uint32_t source=(uint32_t)state.bins*2u*(uint32_t)state.batches;
        expected_frames=(uint32_t)state.fft_size*(uint32_t)state.batches;
        if(iteration==2) assert(expected_frames==8388608);
        float* input_data=(float*)calloc(source,sizeof(float));
        float* output_data=(float*)calloc(expected_frames,sizeof(float));
        float* spectrum_data=(float*)calloc(expected_frames,sizeof(float));
        assert(input_data && output_data && spectrum_data);
        input_data[0]=17;input_data[source-1]=19;
        OrpheusBuffer input={.data=input_data,.format=ORPHEUS_FORMAT_F32,.channels=1,
            .frame_count=source,.frame_capacity=source,.interleaved=true};
        OrpheusBuffer out={.data=output_data,.format=ORPHEUS_FORMAT_F32,.channels=1,
            .frame_count=0,.frame_capacity=expected_frames,.interleaved=true};
        OrpheusBuffer spectrum=out;spectrum.data=spectrum_data;
        const OrpheusBuffer* inputs[]={&input};
        OrpheusBuffer* outputs[]={&out,&spectrum};
        OrpheusContinuation continuation={.emit=emit};
        OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=2,
            .frame_count=8,.valid_frames=8,.sample_rate=48000,.frame_index=123,.epoch=7,
            .timeline_flags=1,.trigger_input=UINT32_MAX,.continuation=&continuation};
        unsigned before=calls;
        assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==before+1);
        assert(output_data[0]==23 && output_data[expected_frames-1]==29);
        assert(spectrum_data[0]==31 && spectrum_data[expected_frames-1]==37);
        assert(input.frame_count==source && ctx.frame_count==8 && ctx.valid_frames==8);
        before=calls;unsigned emitted=emissions;
        --out.frame_capacity;out.frame_count=0;
        assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
        ++out.frame_capacity;--spectrum.frame_capacity;spectrum.frame_count=0;
        assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
        ++spectrum.frame_capacity;--input.frame_count;
        assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
        ++input.frame_count;state.bins=INT_MAX;
        assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
        state.bins=fft[iteration]/2+1;state.batches=0;
        assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
        state.batches=INT_MIN;
        assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
        state.batches=batches[iteration];state.fft_size=INT_MAX;
        assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
        state.fft_size=fft[iteration];
        assert(emissions==emitted);
        ctx.inputs=NULL;ctx.outputs=NULL;
        assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==before+1);
        free(input_data);free(output_data);free(spectrum_data);
    }
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
''', encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


@pytest.mark.parametrize("cpp", [False, True])
@pytest.mark.parametrize("tagged", [False, True])
def test_real_payload_frame_size_parameter_enforces_divisibility_and_range(package, raw, cpp, tagged):
    raw["interfaces"] = {
        "in": {"type": "bulk", "direction": "input", "block_size": 12, "frame_size": "param:span"},
        "span": {"type": "int", "default": 4, "range": [1, 8],
                 "affects_signature": True, "update_policy": "restart_required"},
    }
    if tagged:
        raw["interfaces"]["in"]["layout"] = "halfcomplex"
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
unsigned calls;
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;(void)io;(void)ctx;++calls;return ORPHEUS_OK;
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
extern unsigned calls;
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;OrpheusConfig config={.state_block=&state};
    assert(api->create(&instance,&config)==ORPHEUS_OK && api->prepare(instance,&config)==ORPHEUS_OK);
    float data[12]={0};
    OrpheusBuffer buffer={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_count=12,.frame_capacity=12,.interleaved=true};
    const OrpheusBuffer* inputs[]={&buffer};
    OrpheusProcessContext ctx={.inputs=inputs,.input_count=1,.frame_count=8,.trigger_input=UINT32_MAX};
    assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==1);
    state.span=3;
    assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==2);
    const int invalid[]={0,-1,5,9,12};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        state.span=invalid[i];
        assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==2);
    }
    assert(api->destroy(instance)==ORPHEUS_OK);return 0;
}
''', encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


@pytest.mark.parametrize("cpp", [False, True])
def test_real_implicit_audio_keeps_actual_fragments(package, raw, cpp):
    raw["interfaces"] = {
        "in": {"type": "audio", "direction": "input", "channels": 1,
               "process": True},
        "out": {"type": "audio", "direction": "output", "channels": 1,
                "drive_port_no": 1},
        "elements": {"type": "int", "default": 13, "range": [1, 32],
                     "affects_signature": True, "update_policy": "restart_required"},
    }
    generate_component(save(package, raw))
    assert "_orpheus_payload_out" not in (package / "generated" / "component_shell.c").read_text(
        encoding="utf-8")
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    assert(s->elements==13 && io->out.buffer->frame_count==ctx->frame_count);
    for(unsigned i=0;i<ctx->frame_count;i++) io->out.samples[i]=io->in.samples[i]+1;
    return ORPHEUS_OK;
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;OrpheusConfig config={.state_block=&state};
    assert(api->create(&instance,&config)==ORPHEUS_OK && api->prepare(instance,&config)==ORPHEUS_OK);
    float data[]={1,2,3},result[3]={0};
    OrpheusBuffer input={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_count=3,.frame_capacity=3,.interleaved=true};
    OrpheusBuffer output=input;output.data=result;output.frame_count=0;
    const OrpheusBuffer* inputs[]={&input};OrpheusBuffer* outputs[]={&output};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=3,.valid_frames=3,.trigger_input=0};
    assert(api->process(instance,&ctx)==ORPHEUS_OK && output.frame_count==3);
    for(unsigned i=0;i<3;i++) assert(result[i]==data[i]+1);
    assert(api->destroy(instance)==ORPHEUS_OK);return 0;
}
''', encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


@pytest.mark.parametrize("cpp", [False, True])
def test_real_payload_product_exact_uint32_max_is_safe_with_disconnected_output(package, raw, cpp):
    raw["interfaces"] = {
        "out": {"type": "bulk", "direction": "output", "channels": 1,
                "block_size": "3*1431655765"},
    }
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
unsigned calls;
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;(void)ctx;assert(!io->out.buffer && !io->out.samples);++calls;return ORPHEUS_OK;
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
extern unsigned calls;
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    assert(api->get_descriptor()->ports[0].block_size==UINT32_MAX);
    test_pilot_State state;void* instance=NULL;OrpheusConfig config={.state_block=&state};
    assert(api->create(&instance,&config)==ORPHEUS_OK && api->prepare(instance,&config)==ORPHEUS_OK);
    OrpheusProcessContext ctx={.frame_count=8,.trigger_input=UINT32_MAX};
    assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==1);
    float sample=0;
    OrpheusBuffer output={.data=&sample,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_capacity=1,.interleaved=true};
    OrpheusBuffer* outputs[]={&output};ctx.outputs=outputs;ctx.output_count=1;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==1);
    assert(api->destroy(instance)==ORPHEUS_OK);return 0;
}
''', encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


@pytest.mark.parametrize("cpp", [False, True])
@pytest.mark.parametrize("grouped", [False, True])
@pytest.mark.parametrize("payload", [1, 256])
@pytest.mark.parametrize("declaration", ["literal", "parameter", "reference", "task"])
def test_real_explicit_audio_payload_is_strict_and_independent_of_context(
        package, raw, cpp, grouped, payload, declaration):
    raw["interfaces"] = {
        "in": {"type": "audio", "direction": "input", "channels": 1,
               "block_size": payload, "process": True},
        "out": {"type": "audio", "direction": "output", "channels": 1,
                "block_size": payload, "drive_port_no": 1},
        "frames": {"type": "int", "default": payload, "range": [1, 256],
                   "affects_signature": True, "update_policy": "restart_required"},
    }
    if declaration in {"parameter", "task"}:
        for key in ("in", "out"):
            raw["interfaces"][key]["block_size"] = "param:frames" if declaration == "parameter" else "task:block_size"
    elif declaration == "reference":
        raw["interfaces"]["out"]["block_size"] = "in:block_size"
    if grouped:
        raw["interfaces"]["in"]["count"] = 1
        raw["interfaces"]["out"]["count"] = 1
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
unsigned calls;
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;++calls;
    assert(ctx->frame_count==8 && ctx->valid_frames==8);
    if(IN_VIEW.buffer) assert(IN_VIEW.buffer->frame_count==PAYLOAD);
    if(OUT_VIEW.buffer) {
        assert(OUT_VIEW.buffer->frame_count==PAYLOAD);
        for(unsigned i=0;i<PAYLOAD;i++) OUT_VIEW.samples[i]=IN_VIEW.samples ? IN_VIEW.samples[i]+1 : 0;
    }
    return test_pilot_emit_out(ctx,&OUT_VIEW);
}
'''.replace("PAYLOAD", str(payload)).replace("IN_VIEW", "io->in[0]" if grouped else "io->in").replace(
        "OUT_VIEW", "io->out[0]" if grouped else "io->out"), encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
extern unsigned calls;
static unsigned emissions;
static int emit(void* user,uint32_t drive,const OrpheusDelivery* delivery) {
    (void)user;++emissions;
    assert(drive==1 && delivery->frame_index==123 && delivery->epoch==7);
    if(delivery->buffer) assert(delivery->buffer->frame_count==PAYLOAD);
    return ORPHEUS_OK;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;
    OrpheusConfig config={.state_block=&state,.block_size=PAYLOAD,.sample_rate=48000};
    assert(api->create(&instance,&config)==ORPHEUS_OK && api->prepare(instance,&config)==ORPHEUS_OK);
    float data[PAYLOAD+1],result[PAYLOAD];
    for(unsigned i=0;i<PAYLOAD+1;i++) data[i]=(float)i+1;
    OrpheusBuffer input={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_count=PAYLOAD,.frame_capacity=PAYLOAD+1,.interleaved=true};
    OrpheusBuffer output={.data=result,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_count=0,.frame_capacity=PAYLOAD,.interleaved=true};
    const OrpheusBuffer* inputs[]={&input};OrpheusBuffer* outputs[]={&output};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=8,.valid_frames=8,.sample_rate=48000,.frame_index=123,.epoch=7,
        .trigger_input=0,.continuation=&continuation};
    /* 准备配置已提交，后续改调用者配置不能改变实例载荷。 */
    config.block_size=17;
    assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==1 && emissions==1);
    assert(output.frame_count==PAYLOAD && ctx.frame_count==8 && ctx.valid_frames==8);
    for(unsigned i=0;i<PAYLOAD;i++) assert(result[i]==data[i]+1);
    unsigned before=calls,emitted=emissions;
    input.frame_count=PAYLOAD-1;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
    input.frame_count=PAYLOAD+1;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
    input.frame_count=PAYLOAD;output.frame_count=0;output.frame_capacity=PAYLOAD-1;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
    output.frame_capacity=PAYLOAD;
    input.data=NULL;
    assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
    input.data=data;
    assert(emissions==emitted);
    ctx.inputs=NULL;ctx.outputs=NULL;
    assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==before+1);
    assert(api->destroy(instance)==ORPHEUS_OK);return 0;
}
'''.replace("PAYLOAD", str(payload)), encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


@pytest.mark.parametrize("cpp", [False, True])
@pytest.mark.parametrize("task_field", ["block_size", "sample_rate"])
def test_real_explicit_audio_task_payload_commits_prepare_config_transactionally(package, raw, cpp, task_field):
    raw["interfaces"] = {
        "out": {"type": "audio", "direction": "output", "channels": 1, "block_size": f"task:{task_field}"},
    }
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
unsigned calls,expected;
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;++calls;
    assert(ctx->frame_count==8 && ctx->valid_frames==8);
    assert(io->out.buffer->frame_count==expected);
    io->out.samples[expected-1]=23;
    return ORPHEUS_OK;
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
extern unsigned calls,expected;
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;
    OrpheusConfig config={.state_block=&state,.block_size=1,.sample_rate=1};
    expected=1;
    assert(api->create(&instance,&config)==ORPHEUS_OK && api->prepare(instance,&config)==ORPHEUS_OK);
    float data[256]={0};
    OrpheusBuffer output={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_capacity=256,.interleaved=true};
    OrpheusBuffer* outputs[]={&output};
    OrpheusProcessContext ctx={.outputs=outputs,.output_count=1,.frame_count=8,.valid_frames=8,
        .sample_rate=48000,.trigger_input=UINT32_MAX};
    assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==1 && output.frame_count==1);
    config.TASK_FIELD=256;expected=256;
    assert(api->prepare(instance,&config)==ORPHEUS_OK);
    assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==2 && output.frame_count==256);
    unsigned char before[sizeof(state)];memcpy(before,&state,sizeof(state));
    config.TASK_FIELD=0;
    assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG);
    assert(memcmp(before,&state,sizeof(state))==0);
    assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==3 && output.frame_count==256);
    test_pilot_State rejected;memset(&rejected,0x4c,sizeof(rejected));
    unsigned char untouched[sizeof(rejected)];memcpy(untouched,&rejected,sizeof(rejected));
    config.state_block=&rejected;void* bad=(void*)1;
    assert(api->create(&bad,&config)==ORPHEUS_ERR_INVALID_ARG && !bad);
    assert(memcmp(untouched,&rejected,sizeof(rejected))==0);
    assert(api->destroy(instance)==ORPHEUS_OK);return 0;
}
'''.replace("TASK_FIELD", task_field), encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


def _add_channel_product_interfaces(raw):
    raw["interfaces"] = {
        "in": {"type": "bulk", "direction": "input", "sample_format": "f32",
               "channels": "param:channels", "block_size": 3, "process": True},
        "out": {"type": "bulk", "direction": "output", "sample_format": "f32",
                "channels": "param:channels*2", "block_size": 3, "drive_port_no": 1},
        "channels": {"type": "int", "default": 2, "range": [1, 32],
                     "affects_signature": True, "update_policy": "restart_required"},
    }


@pytest.mark.parametrize("expression", [
    "param:channels*2", "2*param:channels", "param:channels*2*1",
    "param:channels*param:channels", "32768*2", "1*1",
])
def test_channel_product_schema_normalization_cli_and_single_parameter_source(package, raw, expression):
    import jsonschema
    _add_channel_product_interfaces(raw)
    raw["interfaces"]["out"]["channels"] = expression
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    jsonschema.validate(raw, schema)
    path = save(package, raw)
    before = snapshot(package)
    normalized = normalize_component_definition(load_component_definition(path))
    assert normalized["ports"][0]["channels"] == "param:channels"
    assert normalized["ports"][1]["channels"] == expression
    assert [p["id"] for p in normalized["parameters"]] == ["channels"]
    assert normalized["parameters"][0]["range"] == [1, 32]
    assert snapshot(package) == before
    for script in ("validate_component.py", "generate_component.py"):
        result = subprocess.run([str(PYTHON), str(ROOT / "scripts" / script), str(path), "--format", "json"],
                                cwd=package, capture_output=True, encoding="utf-8")
        assert result.returncode == 0, result.stderr + result.stdout
        assert json.loads(result.stdout)["ok"] is True
        if script == "validate_component.py":
            assert snapshot(package) == before
    check_generated(path)
    shell = (package / "generated" / "component_shell.c").read_text(encoding="utf-8")
    assert f'.channels_param="{expression[6:]}"' not in shell


@pytest.mark.parametrize("expression", [
    "param:channels**2", "*param:channels", "param:channels*", "param:channels*0",
    "param:channels*-1", "param:channels*1.5", "param:channels/2", "param:channels+2",
    "param:channels * 2", "param:channels*task:block_size", "param:channels*in:channels",
    "param:channels*2\n",
])
def test_channel_product_invalid_syntax_rejected_by_schema_and_cli_without_writes(package, raw, expression):
    import jsonschema
    _add_channel_product_interfaces(raw)
    raw["interfaces"]["out"]["channels"] = expression
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    result = subprocess.run(
        [str(PYTHON), str(ROOT / "scripts" / "generate_component.py"), str(path), "--format", "json"],
        cwd=package, capture_output=True, encoding="utf-8")
    assert result.returncode == 1
    assert json.loads(result.stdout)["ok"] is False
    assert snapshot(package) == before


@pytest.mark.parametrize("case", [
    "unknown", "float", "bool", "array", "runtime", "non_signature",
    "zero_default", "zero_range", "negative_range", "no_range",
    "channel_limit", "uint32_overflow", "constant_overflow", "oversized_constant",
    "inherited_overflow",
])
def test_channel_product_bad_references_bounds_and_overflows_are_readonly(package, raw, case):
    _add_channel_product_interfaces(raw)
    port, parameter = raw["interfaces"]["out"], raw["interfaces"]["channels"]
    if case == "unknown":
        port["channels"] = "param:missing*2"
    elif case in {"float", "bool"}:
        parameter.update(type=case, default=2.0 if case == "float" else True)
        parameter.pop("range")
    elif case == "array":
        parameter.update(count=1, default=[2], affects_signature=False)
    elif case == "runtime":
        parameter.update(update_policy="block_boundary", affects_signature=False)
    elif case == "non_signature":
        parameter["affects_signature"] = False
    elif case == "zero_default":
        parameter.update(default=0, range=[0, 32])
    elif case in {"zero_range", "negative_range"}:
        parameter["range"][0] = 0 if case == "zero_range" else -1
    elif case == "no_range":
        del parameter["range"]
    elif case == "channel_limit":
        parameter["range"][1] = 32769
    elif case == "uint32_overflow":
        parameter["range"][1] = 2147483647
    elif case == "constant_overflow":
        port["channels"] = "65536*65536"
    elif case == "oversized_constant":
        port["channels"] = "4294967296*1"
    else:
        raw["interfaces"]["in"]["block_size"] = "65536*2"
        port["channels"] = "in:block_size"
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("cpp", [False, True])
@pytest.mark.parametrize("grouped", [False, True])
@pytest.mark.parametrize("port_type", ["bulk", "audio"])
@pytest.mark.parametrize("inherited", [False, True])
def test_real_channel_product_binds_actual_input_output_views_and_enforces_ranges(
        package, raw, cpp, grouped, port_type, inherited):
    _add_channel_product_interfaces(raw)
    for key in ("in", "out"):
        raw["interfaces"][key]["type"] = port_type
        if grouped:
            raw["interfaces"][key]["count"] = 1
    if inherited:
        raw["interfaces"]["in"]["channels"] = "param:channels*2"
        raw["interfaces"]["out"]["channels"] = "in:channels"
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
unsigned calls,expected_channels;
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    ++calls;
    assert(s->channels==(int)expected_channels && ctx->frame_count==8 && ctx->valid_frames==8);
    if(IN_VIEW.buffer) assert(IN_VIEW.buffer->channels==expected_channels*INPUT_FACTOR);
    if(OUT_VIEW.buffer) assert(OUT_VIEW.buffer->channels==expected_channels*2);
    if(IN_VIEW.samples && OUT_VIEW.samples) {
        for(unsigned frame=0;frame<3;frame++)
            for(unsigned channel=0;channel<expected_channels;channel++) {
                float value=IN_VIEW.samples[(frame*expected_channels+channel)*INPUT_FACTOR];
                OUT_VIEW.samples[(frame*expected_channels+channel)*2]=value;
                OUT_VIEW.samples[(frame*expected_channels+channel)*2+1]=-value;
            }
    }
    return test_pilot_emit_out(ctx,&OUT_VIEW);
}
'''.replace("INPUT_FACTOR", "2" if inherited else "1").replace(
        "IN_VIEW", "io->in[0]" if grouped else "io->in").replace(
        "OUT_VIEW", "io->out[0]" if grouped else "io->out"), encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <limits.h>
#include <string.h>
extern unsigned calls,expected_channels;
static unsigned emissions;
static int emit(void* user,uint32_t drive,const OrpheusDelivery* delivery) {
    (void)user;++emissions;assert(drive==1);
    if(delivery->buffer) assert(delivery->buffer->channels==expected_channels*2);
    return ORPHEUS_OK;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    const OrpheusComponentDescriptor* descriptor=api->get_descriptor();
    assert(descriptor->param_count==1);
    assert(descriptor->ports[1].channels==0 && descriptor->ports[1].is_variable);
    assert(!descriptor->ports[1].channels_param);
    test_pilot_State state;void* instance=NULL;OrpheusConfig config={.state_block=&state,.block_size=8};
    assert(api->create(&instance,&config)==ORPHEUS_OK);
    const char* keys[]={"channels"};
    OrpheusValue channels={.type=ORPHEUS_VALUE_INT};
    config.param_ids=keys;config.param_values=&channels;config.param_count=1;
    const unsigned counts[]={1,32};
    for(unsigned iteration=0;iteration<2;iteration++) {
        expected_channels=counts[iteration];channels.value.i32=(int32_t)expected_channels;
        assert(api->prepare(instance,&config)==ORPHEUS_OK);
        float source[3*64],dest[3*64];
        for(unsigned i=0;i<3*64;i++) source[i]=(float)i+1;
        memset(dest,0,sizeof(dest));
        OrpheusBuffer input={.data=source,.format=ORPHEUS_FORMAT_F32,.channels=expected_channels*INPUT_FACTOR,
            .frame_count=3,.frame_capacity=3,.interleaved=true};
        OrpheusBuffer output=input;output.data=dest;output.channels=expected_channels*2;output.frame_count=0;
        const OrpheusBuffer* inputs[]={&input};OrpheusBuffer* outputs[]={&output};
        OrpheusContinuation continuation={.emit=emit};
        OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
            .frame_count=8,.valid_frames=8,.trigger_input=UINT32_MAX,.continuation=&continuation};
        unsigned before=calls,emitted=emissions;
        assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==before+1 && emissions==emitted+1);
        for(unsigned i=0;i<3*expected_channels;i++) {
            assert(dest[2*i]==source[i*INPUT_FACTOR]);
            assert(dest[2*i+1]==-source[i*INPUT_FACTOR]);
        }
        before=calls;++output.channels;
        assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
        --output.channels;++input.channels;
        assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
        --input.channels;output.frame_count=0;output.frame_capacity=2;
        assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
        output.frame_capacity=3;ctx.inputs=NULL;ctx.outputs=NULL;
        const int invalid[]={0,-1,33,INT_MAX,INT_MIN};
        unsigned char previous[sizeof(state)];memcpy(previous,&state,sizeof(state));
        for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
            channels.value.i32=invalid[i];
            assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG);
            assert(memcmp(previous,&state,sizeof(state))==0);
            state.channels=invalid[i];
            assert(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);
            state.channels=(int32_t)expected_channels;
        }
        channels.value.i32=(int32_t)expected_channels;
        assert(api->process(instance,&ctx)==ORPHEUS_OK && calls==before+1);
    }
    assert(api->destroy(instance)==ORPHEUS_OK);return 0;
}
'''.replace("INPUT_FACTOR", "2" if inherited else "1"), encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


@pytest.mark.parametrize("cpp", [False, True])
@pytest.mark.parametrize("channels", ["1*1", "32768*2"])
def test_real_channel_product_constant_lower_and_upper_bounds(package, raw, cpp, channels):
    raw["interfaces"] = {
        "out": {"type": "bulk", "direction": "output", "channels": channels, "block_size": 1},
    }
    expected = 1 if channels == "1*1" else 65536
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;(void)ctx;
    assert(io->out.buffer->channels==EXPECTED);
    io->out.samples[0]=23;io->out.samples[EXPECTED-1]=29;
    return ORPHEUS_OK;
}
'''.replace("EXPECTED", str(expected)), encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <stdlib.h>
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    assert(api->get_descriptor()->ports[0].channels==EXPECTED);
    test_pilot_State state;void* instance=NULL;OrpheusConfig config={.state_block=&state};
    assert(api->create(&instance,&config)==ORPHEUS_OK && api->prepare(instance,&config)==ORPHEUS_OK);
    float* samples=(float*)calloc(EXPECTED,sizeof(float));assert(samples);
    OrpheusBuffer output={.data=samples,.format=ORPHEUS_FORMAT_F32,.channels=EXPECTED,
        .frame_capacity=1,.interleaved=true};
    OrpheusBuffer* outputs[]={&output};
    OrpheusProcessContext ctx={.outputs=outputs,.output_count=1,.frame_count=8,.trigger_input=UINT32_MAX};
    assert(api->process(instance,&ctx)==ORPHEUS_OK && samples[EXPECTED-1]==29);
    free(samples);assert(api->destroy(instance)==ORPHEUS_OK);return 0;
}
'''.replace("EXPECTED", str(expected)), encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


@pytest.mark.parametrize("kind,zero,count", [
    (kind, zero, count)
    for kind, zero in [("float", 0.0), ("int", 0), ("bool", False), ("string", "")]
    for count in [None, 1, 7, 1023] if kind != "string" or count is None
])
def test_readonly_probe_omitted_default_is_typed_normalized_zero_without_mutating_source(
        package, raw, kind, zero, count):
    import jsonschema
    parameter = {"type": kind, "kind": "probe", "access": "read", "readback": True, "persistent": False}
    if count is not None:
        parameter["count"] = count
    raw["interfaces"] = {"observed": parameter}
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    path = save(package, raw)
    before = snapshot(package)
    loaded = load_component_definition(path)
    assert "default" not in loaded["interfaces"]["observed"]
    normalized = normalize_component_definition(loaded)
    expected = zero if count is None else [zero] * count
    actual = normalized["parameters"][0]["default"]
    assert actual == expected
    assert type(actual if count is None else actual[0]) is type(zero)
    assert "default" not in normalized["interfaces"]["observed"]
    assert "default" not in raw["interfaces"]["observed"]
    assert snapshot(package) == before
    for script in ("validate_component.py", "generate_component.py"):
        result = subprocess.run([str(PYTHON), str(ROOT / "scripts" / script), str(path), "--format", "json"],
                                cwd=package, capture_output=True, encoding="utf-8")
        assert result.returncode == 0, result.stderr + result.stdout
        assert json.loads(result.stdout)["ok"] is True
        if script == "validate_component.py":
            assert snapshot(package) == before
    check_generated(path)


@pytest.mark.parametrize("parameter", [
    {"kind": "probe"}, {"kind": "probe", "readonly": True},
    {"kind": "probe", "access": "write"}, {"kind": "probe", "access": "read_write"},
    {"kind": "setting", "access": "read"}, {"kind": "state", "access": "read"},
    {"kind": "bulk", "access": "read"}, {"access": "read", "readonly": True}, {},
])
@pytest.mark.parametrize("array", [False, True])
def test_readonly_probe_default_omission_is_not_inferred_for_other_access_or_kinds(package, raw, parameter, array):
    import jsonschema
    item = {"type": "float", **parameter}
    if array:
        item["count"] = 3
    raw["interfaces"] = {"observed": item}
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("parameter", [
    {"type": "float", "count": 3, "default": 0.0},
    {"type": "float", "count": 3, "default": [0.0, 0.0]},
    {"type": "float", "count": 1, "default": [False]},
    {"type": "int", "count": 2, "default": [0, 0.0]},
    {"type": "bool", "count": 2, "default": [False, 0]},
    {"type": "float", "default": None},
    {"type": "float", "default": False},
    {"type": "float", "count": 2, "default": [0.0, float("nan")]},
    {"type": "float", "range": [1, 8]},
    {"type": "int", "count": 2, "range": [1, 8]},
    {"type": "float", "count": 0},
    {"type": "float", "count": 65537},
    {"type": "float", "count": True},
])
def test_readonly_probe_zero_inference_never_broadcasts_or_bypasses_default_validation(package, raw, parameter):
    raw["interfaces"] = {"observed": {"kind": "probe", "access": "read", **parameter}}
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError):
        generate_component(path)
    result = subprocess.run(
        [str(PYTHON), str(ROOT / "scripts" / "generate_component.py"), str(path), "--format", "json"],
        cwd=package, capture_output=True, encoding="utf-8")
    assert result.returncode == 1
    assert json.loads(result.stdout)["ok"] is False
    assert snapshot(package) == before


@pytest.mark.parametrize("default", [0, 48000])
def test_auto_sample_rate_reference_requires_nonnegative_range_and_preserves_inference(package, raw, default):
    import jsonschema
    raw["interfaces"]["rate"] = {"type": "int", "default": default, "range": [0, 192000],
                                 "affects_signature": True, "update_policy": "restart_required"}
    raw["interfaces"]["in"]["sample_rate"] = "param:rate"
    raw["interfaces"]["out"]["sample_rate"] = "in:sample_rate"
    before = copy.deepcopy(raw)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    view = normalize_component_definition(raw)
    assert raw == before
    assert view["ports"][0]["sample_rate"] == "param:rate"
    assert view["ports"][1]["sample_rate"] == "in:sample_rate"
    path = save(package, raw)
    generate_component(path)
    check_generated(path)


def test_bulk_slot_key_is_derived_for_control_parameter_and_real_plan(package, raw):
    from orpheus_core.compiler import GraphCompiler
    from orpheus_core.project import ControlConnection, Graph, Node, PortRef, Project
    from orpheus_core.registry import Registry

    raw["interfaces"]["gain"].update(default=[1.0, 2.0], count=2, shape=[2],
        kind="bulk", bindable=True, control_source=True, readback=True,
        bulk={"slot_key": "gain_storage", "double_bank": True})
    before = copy.deepcopy(raw)
    view = normalize_component_definition(raw)
    assert raw == before
    parameter = next(p for p in view["parameters"] if p["id"] == "gain")
    assert parameter["bulk_slot"] == "gain_storage"
    registry = Registry()
    registry.add_manifest(save(package, raw))
    project = Project(graph=Graph(nodes={
        "source": Node("source", raw["id"]), "target": Node("target", raw["id"]),
    }), control_connections=[ControlConnection(PortRef("source", "gain"), PortRef("target", "gain"))])
    plan = GraphCompiler(registry).compile(project)
    assert plan.control_links[0]["src_slot"] == "gain_storage"
    assert plan.control_links[0]["dst_slot"] == "gain_storage"
    assert plan.control_links[0]["count"] == 2


@pytest.mark.parametrize("field,reference", [
    ("channels", "param:rate"), ("block_size", "param:rate"), ("frame_size", "param:rate"),
    ("channels", "in:sample_rate"), ("block_size", "in:sample_rate"),
])
def test_auto_sample_rate_cannot_become_zero_payload_size(package, raw, field, reference):
    raw["interfaces"]["rate"] = {"type": "int", "default": 0, "range": [0, 192000],
                                 "affects_signature": True, "update_policy": "restart_required"}
    raw["interfaces"]["in"]["sample_rate"] = "param:rate"
    raw["interfaces"]["out"]["type"] = "bulk"
    raw["interfaces"]["out"][field] = reference
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError, match="正数|载荷尺寸"):
        generate_component(path)
    assert snapshot(package) == before


@pytest.mark.parametrize("default,bounds", [(0, None), (-1, [-1, 192000]), (1, [-1, 192000])])
def test_auto_sample_rate_rejects_negative_or_unbounded_zero_parameter(package, raw, default, bounds):
    raw["interfaces"]["rate"] = {"type": "int", "default": default,
                                 "affects_signature": True, "update_policy": "restart_required"}
    if bounds is not None:
        raw["interfaces"]["rate"]["range"] = bounds
    raw["interfaces"]["in"]["sample_rate"] = "param:rate"
    path = save(package, raw)
    before = snapshot(package)
    with pytest.raises(ComponentDefinitionError, match="正数"):
        generate_component(path)
    assert snapshot(package) == before


def test_readonly_probe_explicit_defaults_and_signature_reference_validation_remain_strict(package, raw):
    import jsonschema
    raw["interfaces"] = {
        "observed": {"type": "float", "count": 2, "kind": "probe", "access": "read", "default": [1.5, 2.5]},
    }
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    assert normalize_component_definition(raw)["parameters"][0]["default"] == [1.5, 2.5]
    del raw["interfaces"]["observed"]["default"]
    generated = normalize_component_definition(raw)
    generated["parameters"][0]["default"][0] = 9
    assert normalize_component_definition(raw)["parameters"][0]["default"] == [0.0, 0.0]
    raw["interfaces"] = {
        "in": {"type": "bulk", "direction": "input", "channels": "param:observed"},
        "observed": {"type": "int", "kind": "probe", "access": "read",
                     "affects_signature": True, "update_policy": "restart_required"},
    }
    with pytest.raises(ComponentDefinitionError, match="正数"):
        generate_component(save(package, raw))


@pytest.mark.parametrize("cpp", [False, True])
@pytest.mark.parametrize("count", [1, 1023])
def test_real_readonly_probe_inferred_defaults_register_typed_arrays_and_reject_setters(package, raw, cpp, count):
    raw["interfaces"] = {
        name: {"type": kind, "kind": "probe", "access": "read", "readback": True, "persistent": False,
               **({"count": size} if size is not None else {})}
        for name, kind, size in [
            ("values", "float", count), ("integers", "int", 2), ("switches", "bool", 2),
            ("level", "float", None), ("position", "int", None), ("ready", "bool", None), ("text", "string", None)]
    }
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <string.h>
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)io;(void)ctx;
    for(unsigned i=0;i<COUNT;i++) s->values[i]=(float)i+1;
    s->integers[0]=-3;s->switches[1]=true;
    s->level=2.5f;s->position=17;s->ready=true;strcpy(s->text,"ready");
    return ORPHEUS_OK;
}
'''.replace("COUNT", str(count)), encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
static test_pilot_State* current;
static unsigned registrations;
static OrpheusSlotId add(void* user,const OrpheusSlotInfo* slot) {
    (void)user;++registrations;
    assert(slot->kind==ORPHEUS_SLOT_PROBE);
    assert((slot->flags&ORPHEUS_SLOT_READBACK) && !(slot->flags&ORPHEUS_SLOT_PERSISTENT));
    if(strcmp(slot->key,"values")==0) {
        assert(slot->count==COUNT && slot->type==ORPHEUS_VALUE_FLOAT);
        assert((char*)current+slot->offset==(char*)current->values);
    } else if(strcmp(slot->key,"integers")==0) {
        assert(slot->count==2 && slot->type==ORPHEUS_VALUE_INT);
        assert((char*)current+slot->offset==(char*)current->integers);
    } else if(strcmp(slot->key,"switches")==0) {
        assert(slot->count==2 && slot->type==ORPHEUS_VALUE_BOOL);
        assert((char*)current+slot->offset==(char*)current->switches);
    }
    return registrations;
}
static void check_zero(test_pilot_State* state) {
    for(unsigned i=0;i<COUNT;i++) assert(state->values[i]==0.0f);
    assert(state->integers[0]==0 && state->integers[1]==0);
    assert(!state->switches[0] && !state->switches[1]);
    assert(state->level==0.0f && state->position==0 && !state->ready && state->text[0]==0);
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;OrpheusConfig config={.state_block=&state};
    assert(api->create(&instance,&config)==ORPHEUS_OK);check_zero(&state);
    assert(api->prepare(instance,&config)==ORPHEUS_OK);check_zero(&state);
    current=&state;OrpheusRegistry registry={.add=add};
    assert(api->register_slots(instance,&registry)==ORPHEUS_OK && registrations==7);
    OrpheusProcessContext ctx={.frame_count=8,.trigger_input=UINT32_MAX};
    assert(api->process(instance,&ctx)==ORPHEUS_OK);
    assert(state.values[COUNT-1]==(float)COUNT && state.integers[0]==-3 && state.switches[1]);
    OrpheusValue readback;
    assert(api->get_parameter(instance,"level",&readback)==ORPHEUS_OK && readback.value.f32==2.5f);
    assert(api->get_parameter(instance,"position",&readback)==ORPHEUS_OK && readback.value.i32==17);
    assert(api->get_parameter(instance,"ready",&readback)==ORPHEUS_OK && readback.value.b);
    assert(api->get_parameter(instance,"text",&readback)==ORPHEUS_OK && strcmp(readback.value.str,"ready")==0);
    unsigned char before[sizeof(state)];memcpy(before,&state,sizeof(state));
    const char* keys[]={"values","integers","switches","level","position","ready","text"};
    for(unsigned i=0;i<7;i++) {
        assert(api->set_parameter(instance,keys[i],&readback)==ORPHEUS_ERR_UNSUPPORTED);
        assert(memcmp(before,&state,sizeof(state))==0);
    }
    assert(api->prepare(instance,&config)==ORPHEUS_OK);check_zero(&state);
    assert(api->destroy(instance)==ORPHEUS_OK);return 0;
}
'''.replace("COUNT", str(count)), encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)
