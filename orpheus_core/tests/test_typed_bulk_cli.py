"""命令行按槽类型读写 BULK，拒绝无效整数和不完整数组。"""
from dataclasses import asdict
import json
from pathlib import Path
import subprocess
import wave

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def cli_project(tmp_path_factory):
    from test_generated_double_bank import _build

    directory = tmp_path_factory.mktemp("typed_bulk_cli")
    with wave.open(str(directory / "input.wav"), "wb") as stream:
        stream.setnchannels(2)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(bytes(32 * 2 * 2))
    project = Project(metadata={"name": "BULK命令行类型验收"})
    project.tasks = {"default": Task(id="default", sample_rate=48000, block_size=32)}
    project.graph = Graph(nodes={
        "source": Node(id="source", component="orpheus.builtin.wav_in",
                       params={"channels": 2, "file_path": "input.wav"}),
        "delay": Node(id="delay", component="orpheus.builtin.delay_line",
                      params={"channels": 2, "max_delay_samples": 64,
                              "delay_vector_mode": 1, "initial_delays_i32": "1,2"}),
        "sink": Node(id="sink", component="orpheus.builtin.wav_out",
                     params={"channels": 2, "file_path": "output.wav"}),
    }, connections=[
        Connection(from_ref=PortRef("source", "out"), to_ref=PortRef("delay", "in")),
        Connection(from_ref=PortRef("delay", "out"), to_ref=PortRef("sink", "in")),
    ])
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    plan = GraphCompiler(registry).compile(project)
    path = directory / "plan.json"
    path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = directory / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    result = subprocess.run(
        ["cmake", "--build", str(ROOT / "build"), "--target", "orpheus_runtime"],
        capture_output=True, text=True, encoding="utf-8",
    )
    assert result.returncode == 0, result.stdout + result.stderr
    executable = _build(generated, directory)
    ids = {e["key"]: str(e["id"]) for e in plan.id_map if e["node"] == "delay"}
    return directory, path, executable, ids


@pytest.mark.parametrize("engine", ["dynamic", "generated-id", "generated-name"])
def test_typed_bulk_cli(cli_project, engine):
    directory, path, generated, ids = cli_project

    def run(key, values=None, *, commit=False, count=None):
        if engine == "dynamic":
            command = [str(ROOT / "build" / "orpheus_runtime.exe"),
                       str(path), str(ROOT / "build" / "components")]
            write, read = ["--rwb", ids[key]], ["--rgb", ids[key]]
        elif engine == "generated-id":
            command = [str(generated), "0"]
            write, read = ["--write-bulk-id", ids[key]], ["--read-bulk-id", ids[key]]
        else:
            command = [str(generated), "0"]
            write = ["--write-bulk", "delay", key]
            read = ["--read-bulk", "delay", key]
        if values is not None:
            command += write + [str(len(values) if count is None else count)] + values
        if commit:
            command += ["--run", "1"]
        command += read
        return subprocess.run(command, cwd=directory, capture_output=True,
                              text=True, encoding="utf-8")

    def values(result):
        assert result.returncode == 0, result.stdout + result.stderr
        line = next(line for line in result.stdout.splitlines() if line.startswith("BULKVALUE "))
        return line.split()[3 if engine == "generated-name" else 2:]

    assert values(run("delays_i32")) == ["1", "2"]
    assert values(run("delays_i32", ["3", "7"])) == ["1", "2"]
    assert values(run("delays_i32", ["3", "7"], commit=True)) == ["3", "7"]
    assert values(run("delays_i32", ["0", "64"], commit=True)) == ["0", "64"]
    assert values(run("delays_i32", ["9"], commit=True)) == ["9", "2"]
    assert [float(v) for v in values(run("delays_samples", ["1.25", "2.5"], commit=True))] == [1.25, 2.5]
    for invalid in (["1.5"], ["2147483648"], ["-2147483649"], ["-1"],
                    ["65"], ["3x"], ["1", "2", "3"], []):
        result = run("delays_i32", invalid)
        assert result.returncode != 0, (invalid, result.stdout, result.stderr)
        assert "ERR" in result.stdout + result.stderr
    for count in ("-1", "2x", "999999999999999999999", 2):
        result = run("delays_i32", ["1"], count=count)
        assert result.returncode != 0, (count, result.stdout, result.stderr)
        assert "ERR" in result.stdout + result.stderr
    assert values(run("delays_i32")) == ["1", "2"]
