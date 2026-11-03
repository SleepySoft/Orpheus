"""数据尺寸与处理量子不同，纯计算图仍按Task节拍同轮执行。"""
from dataclasses import asdict
import json
from pathlib import Path
import shutil
import struct
import subprocess

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).parent / "fixtures/array_pair"


def verify_complementary_coefficients(tmp_path, *, channels=2, elements=129, step=128, total=1.0, reference=None):
    from test_generated_double_bank import _build
    registry = Registry([ROOT / "components"]); registry.scan()
    registry.add_manifest(FIXTURE / "component.yaml")
    nodes = {
        "source": Node(id="source", component="orpheus.test.array_pair",
                       params={"channels": channels, "elements": elements, "mode": 0}),
        "math": Node(id="math", component="orpheus.builtin.complementary_coefficients",
                     params={"channels": channels, "elements": elements, "total": total}),
        "capture": Node(id="capture", component="orpheus.test.array_pair",
                        params={"channels": channels, "elements": elements, "mode": 1}),
    }
    edges = [Connection(PortRef.parse(a), PortRef.parse(b)) for a, b in
             (("source:out", "math:in"), ("source:out", "capture:a"), ("math:out", "capture:b"))]
    project = Project(metadata={"name": "互补数组计算"}, tasks={
        "default": Task(id="default", sample_rate=48000, block_size=step)}, graph=Graph(nodes=nodes, connections=edges))
    plan = GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"] == step and set(plan.schedule["periods"].values()) == {1}
    for config in plan.node_configs.values():
        assert config["frames"] == config.get("frame_step", config["frames"]) == step
    assert plan.execution_order.index("source")<plan.execution_order.index("math")<plan.execution_order.index("capture")
    assert all(b["type"]=="bulk" and b["frame_count"]==elements for b in plan.buffers.values())
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src/array_test.c").write_bytes((ROOT / "tests/halfcomplex_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as file:
        file.write("\nadd_executable(array_test src/array_test.c)\n"
                   "target_compile_definitions(array_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                   "target_link_libraries(array_test PRIVATE orpheus_graph)\n"
                   f'add_library(array_pair_dynamic SHARED "{(FIXTURE / "src/array_pair.c").as_posix()}")\n'
                   f'target_include_directories(array_pair_dynamic PRIVATE "{(FIXTURE / "include").as_posix()}")\n'
                   "target_compile_definitions(array_pair_dynamic PRIVATE ORPHEUS_BUILDING_COMPONENT)\n"
                   'set_target_properties(array_pair_dynamic PROPERTIES PREFIX "lib" OUTPUT_NAME "orpheus_test_array_pair" '
                   'WINDOWS_EXPORT_ALL_SYMBOLS ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components" '
                   'LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components")\n')
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target",
                             "halfcomplex_runner", "orpheus_builtin_complementary_coefficients"],
                            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    libraries = generated / "build/components"
    for library in (ROOT / "build/components").glob("*orpheus_builtin_complementary_coefficients.*"):
        if library.suffix in (".dll", ".so", ".dylib"):
            shutil.copy2(library, libraries / library.name)
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    count = channels*elements
    args = ["capture", str(count), str(count*2), str(step)]
    baseline = None
    for mode in ("global", "task"):
        for command in ([str(ROOT / "build/tests/halfcomplex_runner.exe"), str(plan_path), str(libraries), mode, str(step), *args],
                        [str(generated / "build/array_test.exe"), mode, str(step), *args]):
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout+result.stderr
            data = (tmp_path / "halfcomplex.bin").read_bytes()
            if baseline is None: baseline = data
            assert data == baseline and len(data)==count*12*3
            cursor = 0
            for block in range(3):
                packed = data[cursor:cursor+count*4]; cursor += count*4
                expected_input = struct.pack(f"<{count}f", *[((i*17+block*13)%129-64)*.03125 for i in range(count)])
                assert packed == expected_input
                if reference is not None:
                    expected = reference(packed)
                else:
                    values = struct.unpack(f"<{count}f", packed)
                    expected = b"".join(packed[row*channels*4:(row+1)*channels*4]+struct.pack(
                        f"<{channels}f", *(total-x for x in values[row*channels:(row+1)*channels])) for row in range(elements))
                assert len(expected)==count*8 and data[cursor:cursor+count*8]==expected, (mode, block)
                cursor += count*8


@pytest.mark.parametrize("channels,elements,step,total", [(2,129,128,1.0),(2,387,384,1.0),(1,7,32,-0.5)])
def test_complementary_coefficients_four_entries(tmp_path, channels, elements, step, total):
    verify_complementary_coefficients(tmp_path, channels=channels, elements=elements, step=step, total=total)
