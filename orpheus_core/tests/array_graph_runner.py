"""同Task数组算子的共享四入口采集驱动。"""
from dataclasses import asdict
import json
import os
from pathlib import Path
import shutil
import subprocess

from orpheus_core.generator import CodeGenerator

ROOT = Path(__file__).resolve().parents[2]


def partition_component_sources(registry, plan):
    shared, imported = [], []
    for component in sorted({cfg["component"] for cfg in plan.node_configs.values()}):
        info = registry.get(component)
        assert info is not None
        target = shared if info.root_dir.resolve().is_relative_to(ROOT / "components") else imported
        target.append(component)
    return shared, imported


def use_linear_call_chain(plan):
    # 已声明的调用不改写；旧数值夹具按节拍分段，段内续调，入口只驱动各段。
    if plan.flow_mode == "task_sequence":
        return
    order = plan.execution_order
    if not order or not plan.schedule or len(plan.tasks) != 1:
        raise ValueError("数组数值夹具需要单Task、非空入口和完整节拍声明。")
    periods = plan.schedule["periods"]
    if periods[order[0]] != 1:
        raise ValueError("数组数值夹具的入口必须按Task基本节拍调用。")
    plan.entry_nodes = order[:1]
    plan.call_graph = {nid: [] for nid in order}
    for previous, nid in zip(order, order[1:]):
        period = periods[nid]
        same = period == periods[previous]
        caller = previous if same else order[0]
        plan.call_graph[caller].append(
            {"node": nid, "every": 1 if same else period, "phase": 0 if same else period-1})
    for task in plan.tasks:
        task["entry_nodes"] = [nid for nid in plan.entry_nodes if nid in task["nodes"]]


def execute_array_graph(tmp_path, registry, plan, captures, *, blocks=3, calls_per_block=1):
    from test_generated_double_bank import _build
    assert isinstance(blocks,int) and 1<=blocks<=100000
    if type(calls_per_block) is not int or not 1 <= calls_per_block <= 1024:
        raise ValueError("每块采集调用次数必须是1至1024的整数。")
    use_linear_call_chain(plan)
    if len(plan.tasks) != 1:
        raise ValueError("数组数值夹具需要且仅支持一个Task。")
    step = plan.schedule["tick"]
    generated = tmp_path / "generated"
    components, imported = partition_component_sources(registry, plan)
    generator = CodeGenerator(registry, ROOT)
    generator.generate(plan, generated)
    task_symbol = generator._sanitized_node_id(plan.tasks[0]["id"])
    (generated / "src/array_test.c").write_bytes((ROOT / "tests/halfcomplex_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as file:
        file.write("\nadd_executable(array_test src/array_test.c)\n"
                   "target_compile_definitions(array_test PRIVATE ORPHEUS_TEST_GENERATED "
                   f"ORPHEUS_TEST_TASK_FUNCTION=orpheus_generated_process_task_{task_symbol})\n"
                   "target_link_libraries(array_test PRIVATE orpheus_graph)\n")
        for component in imported:
            info = registry.get(component)
            assert info is not None
            target = component.replace(".", "_")+"_dynamic"
            sources = " ".join('"'+(info.root_dir / name).as_posix()+'"' for name in info.manifest["sources"])
            includes = " ".join(f'"{path.as_posix()}"' for path in registry.include_dirs(info))
            file.write(f"\nadd_library({target} SHARED {sources})\n"
                       f"target_include_directories({target} PRIVATE {includes})\n"
                       f"target_compile_definitions({target} PRIVATE ORPHEUS_BUILDING_COMPONENT)\n"
                       f'set_target_properties({target} PROPERTIES PREFIX "lib" OUTPUT_NAME "{component.replace(".", "_")}" '
                       'WINDOWS_EXPORT_ALL_SYMBOLS ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components" '
                       'LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components")\n')
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
                                    env={**os.environ, "ORPHEUS_TEST_BLOCKS": str(blocks),
                                         "ORPHEUS_TEST_CALLS_PER_BLOCK": str(calls_per_block)})
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
