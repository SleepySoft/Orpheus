"""Tests for the rate_sync multi-rate async merge component."""

from __future__ import annotations

import math
from pathlib import Path

import pytest

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def compiler():
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    return GraphCompiler(registry)


def conn(a: str, b: str) -> Connection:
    return Connection(from_ref=PortRef.parse(a), to_ref=PortRef.parse(b))


def make_project(nodes, connections, tasks) -> Project:
    project = Project(metadata={"name": "t"})
    project.tasks = {t.id: t for t in tasks}
    project.graph = Graph(nodes={n.id: n for n in nodes}, connections=connections)
    return project


def task(tid: str, block: int):
    from orpheus_core.project import Task

    return Task(id=tid, sample_rate=48000, block_size=block)


def test_rate_sync_merges_different_base_blocks(compiler):
    """rate_sync accepts two inputs at different base rates and emits their LCM."""
    project = make_project(
        [
            Node(id="a", component="orpheus.builtin.signal_gen",
                 params={"frequency": 100.0, "amplitude": 0.1, "channels": 1}),
            Node(id="b", component="orpheus.builtin.signal_gen",
                 params={"frequency": 100.0, "amplitude": 0.1, "channels": 1}),
            Node(id="sync", component="orpheus.builtin.rate_sync",
                 params={"channels": 1, "mode": 0, "buffer_length": 0}),
            Node(id="o", component="orpheus.builtin.wav_out",
                 params={"channels": 1, "sample_rate": 48000, "file_path": "out.wav"}),
        ],
        [conn("a:out", "sync:in0"), conn("b:out", "sync:in1"), conn("sync:out", "o:in")],
        [task("tidA", 24), task("tidB", 32)],
    )
    # assign tasks to nodes
    for n in project.graph.nodes.values():
        if n.id in ("a", "sync", "o"):
            n.task = "tidA"
        elif n.id == "b":
            n.task = "tidB"
    plan = compiler.compile(project)
    cfg = plan.node_configs["sync"]
    assert cfg["block_size"] == math.lcm(24, 32)
    assert cfg["output_port_block_sizes"]["out"] == math.lcm(24, 32)
    assert cfg["divisor"] == 1


def test_mixer_rejects_different_base_blocks(compiler):
    """A non-merge multi-input node still rejects differing block sizes."""
    project = make_project(
        [
            Node(id="a", component="orpheus.builtin.signal_gen",
                 params={"frequency": 440.0, "amplitude": 0.1, "channels": 1}),
            Node(id="b", component="orpheus.builtin.signal_gen",
                 params={"frequency": 440.0, "amplitude": 0.1, "channels": 1}),
            Node(id="m", component="orpheus.builtin.mixer",
                 params={"channels": 1, "gain0": 0.0, "gain1": 0.0}),
            Node(id="o", component="orpheus.builtin.wav_out",
                 params={"channels": 1, "sample_rate": 48000, "file_path": "out.wav"}),
        ],
        [conn("a:out", "m:in0"), conn("b:out", "m:in1"), conn("m:out", "o:in")],
        [task("tidA", 24), task("tidB", 32)],
    )
    for n in project.graph.nodes.values():
        n.task = "tidA" if n.id in ("a", "m", "o") else "tidB"
    with pytest.raises(CompileError):
        compiler.compile(project)
