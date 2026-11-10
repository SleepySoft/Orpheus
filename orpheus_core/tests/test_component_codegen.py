"""独立组件工具的声明、文件事务、CLI 和真实 ABI 调用验证。"""

import copy
import json
import os
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

import pytest
import yaml

from orpheus_core.component_definition import (
    ComponentDefinitionError,
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
                   "channels": 2, "block_size": 32, "process": True},
            "out": {"direction": "output", "type": "audio", "channels": 2,
                    "block_size": "in:block_size", "drive_port_no": 1, "drive_divide": 2},
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
    with pytest.raises(ComponentDefinitionError, match="驱动编号重复"):
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


@pytest.mark.parametrize("feature", ["count", "format", "string", "smoothed"])
def test_unsupported_generation_is_explicit_and_readonly(package, raw, feature):
    if feature == "count":
        raw["interfaces"]["in"]["count"] = "param:channels"
    elif feature == "format":
        raw["interfaces"]["in"]["sample_format"] = "i16"
    elif feature == "string":
        raw["interfaces"]["text"] = {"type": "string", "default": ""}
    else:
        raw["interfaces"]["gain"]["update_policy"] = "smoothed"
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
    del raw["interfaces"]["in"]["block_size"], raw["interfaces"]["out"]["block_size"]
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


def test_execution_metadata_explicitly_rejected(raw):
    import jsonschema
    raw["execution"] = {"realtime_safe": True}
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)
    with pytest.raises(ComponentDefinitionError, match="execution"):
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
    del raw["interfaces"]["in"]["block_size"], raw["interfaces"]["out"]["block_size"]
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
