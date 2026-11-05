"""标量计算、表源和插值的真实C契约，不通过音频或块末控制链模拟。"""
from pathlib import Path
import re
import struct
import subprocess

from array_graph_runner import execute_array_graph
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection, DriveConnection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]


def test_scalar_primitives_native_contract(tmp_path):
    match = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$",
                      (ROOT / "build/CMakeCache.txt").read_text(encoding="utf-8"), re.M)
    assert match
    compiler = match[1]
    includes = ["-I" + str(ROOT / "orpheus_abi/include")]
    objects = []
    for name, entry in (("scalar_math", "scalar_api"), ("bulk_constant", "constant_api"),
                        ("bulk_lookup", "lookup_api"), ("scalar_memory", "memory_api")):
        directory = ROOT / "components/orpheus/builtin" / name
        includes += ["-I" + str(directory / "include")]
        target = tmp_path / (name + ".o")
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-DORPHEUS_BUILDING_COMPONENT", "-DORPHEUS_ENTRY_NAME=" + entry,
                   *includes, "-c", str(directory / "src" / (name + ".c")), "-o", str(target)]
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
        objects.append(str(target))
    executable = tmp_path / "scalar_primitives.exe"
    result = subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", *includes,
                             str(ROOT / "tests/scalar_primitives_smoke.c"), *objects, "-lm",
                             "-o", str(executable)], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(executable)], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


def test_scalar_primitives_explicit_chain_four_entries(tmp_path):
    registry = Registry([ROOT / "components"])
    registry.scan()
    for name in ("array_pair", "tensor_pair_capture"):
        registry.add_manifest(Path(__file__).parent / "fixtures" / name / "component.yaml")
    nodes = {
        "clock": Node("clock", "orpheus.builtin.task_source", params={"frequency_hz": 750}),
        "source": Node("source", "orpheus.test.array_pair", params={"channels": 1, "elements": 1}),
        "axis": Node("axis", "orpheus.builtin.bulk_constant", params={"elements": 3, "values": "-2,0,2"}),
        "values": Node("values", "orpheus.builtin.bulk_constant", params={"elements": 3, "values": "-4,0,6"}),
        "positive": Node("positive", "orpheus.builtin.scalar_math", params={"operation": 1, "b": 0.0}),
        "lookup": Node("lookup", "orpheus.builtin.bulk_lookup", params={"points": 3}),
        "capture": Node("capture", "orpheus.test.tensor_pair_capture",
                        params={"channels_a": 1, "frames_a": 1, "channels_b": 1, "frames_b": 1}),
    }
    pairs = [("source:out", "positive:a"), ("positive:out", "lookup:x"),
             ("axis:out", "lookup:axis"), ("values:out", "lookup:values"),
             ("source:out", "capture:a"), ("lookup:out", "capture:b")]
    order = list(nodes)
    project = Project(tasks={"default": Task("default", sample_rate=48000, block_size=64)},
        flow_mode="task_sequence", graph=Graph(nodes=nodes, connections=[
            Connection(PortRef.parse(a), PortRef.parse(b)) for a, b in pairs]),
        drive_connections=[DriveConnection(PortRef(a, "out"), PortRef(b, "in"))
                           for a, b in zip(order, order[1:])])
    plan = GraphCompiler(registry).compile(project)
    assert plan.entry_nodes == ["clock"]
    records = execute_array_graph(tmp_path, registry, plan, {"capture": (1, 1)}, blocks=24)["capture"]
    for raw, actual in records:
        x = max(struct.unpack("<f", raw)[0], 0.0)
        t = struct.unpack("<f", struct.pack("<f", x / 2))[0]
        expected = 0.0 if x > 2 else t * 6
        assert actual == struct.pack("<f", expected)


def test_scalar_memory_feedback_four_entries(tmp_path):
    registry = Registry([ROOT / "components"]); registry.scan()
    registry.add_manifest(Path(__file__).parent / "fixtures/tensor_pair_capture/component.yaml")
    nodes = {
        "clock": Node("clock", "orpheus.builtin.task_source", params={"frequency_hz": 750}),
        "memory": Node("memory", "orpheus.builtin.scalar_memory",
                       params={"initial": 3.0, "write_after_call": True}),
        "sum": Node("sum", "orpheus.builtin.scalar_math", params={"operation": 6, "b": 2.0}),
        "capture": Node("capture", "orpheus.test.tensor_pair_capture",
                        params={"channels_a": 1, "frames_a": 1, "channels_b": 1, "frames_b": 1}),
    }
    pairs = [("memory:out", "sum:a"), ("sum:out", "memory:in"),
             ("memory:out", "capture:a"), ("sum:out", "capture:b")]
    order = list(nodes)
    project = Project(tasks={"default": Task("default", sample_rate=48000, block_size=64)},
        flow_mode="task_sequence", graph=Graph(nodes=nodes, connections=[
            Connection(PortRef.parse(a), PortRef.parse(b)) for a, b in pairs]),
        drive_connections=[DriveConnection(PortRef(a, "out"), PortRef(b, "in"))
                           for a, b in zip(order, order[1:])])
    plan = GraphCompiler(registry).compile(project)
    records = execute_array_graph(tmp_path, registry, plan, {"capture": (1, 1)}, blocks=24)["capture"]
    for i, (old, new) in enumerate(records):
        assert old == struct.pack("<f", 3.0 + 2 * i)
        assert new == struct.pack("<f", 5.0 + 2 * i)
