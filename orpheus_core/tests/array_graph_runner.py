"""同Task数组算子的共享四入口采集驱动。"""
from dataclasses import asdict
import json
import os
from pathlib import Path
import shutil
import subprocess

from orpheus_core.generator import CodeGenerator

ROOT = Path(__file__).resolve().parents[2]


def use_linear_call_chain(plan):
    # 数值验证显式声明一次调用链，数据会合不承担执行同步/去重。
    order = plan.execution_order
    plan.entry_nodes = order[:1]
    plan.call_graph = {
        nid: ([{"node": order[i + 1], "every": 1, "phase": 0}]
              if i + 1 < len(order) else [])
        for i, nid in enumerate(order)
    }
    for task in plan.tasks:
        task["entry_nodes"] = [nid for nid in plan.entry_nodes if nid in task["nodes"]]


def execute_array_graph(tmp_path, registry, plan, captures, *, blocks=3):
    from test_generated_double_bank import _build
    assert isinstance(blocks,int) and 1<=blocks<=100000
    use_linear_call_chain(plan)
    step = plan.schedule["tick"]
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src/array_test.c").write_bytes((ROOT / "tests/halfcomplex_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as file:
        file.write("\nadd_executable(array_test src/array_test.c)\n"
                   "target_compile_definitions(array_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                   "target_link_libraries(array_test PRIVATE orpheus_graph)\n")
        for component in sorted({cfg["component"] for cfg in plan.node_configs.values()
                                 if not cfg["component"].startswith("orpheus.builtin.")}):
            info = registry.get(component)
            assert info is not None
            target = component.replace(".", "_")+"_dynamic"
            sources = " ".join('"'+(info.root_dir / name).as_posix()+'"' for name in info.manifest["sources"])
            file.write(f"\nadd_library({target} SHARED {sources})\n"
                       f'target_include_directories({target} PRIVATE "{(info.root_dir / "include").as_posix()}")\n'
                       f"target_compile_definitions({target} PRIVATE ORPHEUS_BUILDING_COMPONENT)\n"
                       f'set_target_properties({target} PROPERTIES PREFIX "lib" OUTPUT_NAME "{component.replace(".", "_")}" '
                       'WINDOWS_EXPORT_ALL_SYMBOLS ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components" '
                       'LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components")\n')
    components = sorted({cfg["component"] for cfg in plan.node_configs.values()
                         if cfg["component"].startswith("orpheus.builtin.")})
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", "halfcomplex_runner",
                             *[component.replace(".", "_") for component in components]],
                            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    libraries = generated / "build/components"
    for component in components:
        for library in (ROOT / "build/components").glob("*"+component.replace(".", "_")+".*"):
            if library.suffix in (".dll", ".so", ".dylib"):
                shutil.copy2(library, libraries / library.name)
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    args = [value for node, (a, b) in captures.items()
            for value in (node, str(a), str(b), str(plan.node_configs[node]["frames"]))]
    baseline = None
    expected_size = sum(a+b for a, b in captures.values())*4*blocks
    for mode in ("global", "task"):
        for command in ([str(ROOT / "build/tests/halfcomplex_runner.exe"), str(plan_path), str(libraries), mode, str(step), *args],
                        [str(generated / "build/array_test.exe"), mode, str(step), *args]):
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8",
                                    env={**os.environ,"ORPHEUS_TEST_BLOCKS":str(blocks)})
            assert result.returncode == 0, result.stdout+result.stderr
            data = (tmp_path / "halfcomplex.bin").read_bytes()
            if baseline is None: baseline = data
            assert data == baseline and len(data)==expected_size
    records = {node: [] for node in captures}
    cursor = 0
    for block in range(blocks):
        for node, (a, b) in captures.items():
            packed = baseline[cursor:cursor+a*4]; cursor += a*4
            decoded = baseline[cursor:cursor+b*4]; cursor += b*4
            records[node].append((packed,decoded))
    return records
