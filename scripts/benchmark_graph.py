"""Build and run a deterministic large graph, then print machine-readable timing data."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import tempfile
import time
from dataclasses import asdict
from pathlib import Path

from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[1]


def make_project(gain_nodes: int, block_size: int) -> Project:
    project = Project(sample_rate=48000, block_size=block_size, metadata={"name": "benchmark"})
    nodes = [
        Node(
            id="source",
            component="orpheus.builtin.signal_gen",
            params={"frequency": 440.0, "amplitude": 0.1, "channels": 2},
        )
    ]
    connections = []
    previous = "source:out"
    for index in range(gain_nodes):
        node_id = f"gain_{index}"
        nodes.append(Node(
            id=node_id,
            component="orpheus.builtin.gain",
            params={"gain_db": 0.0, "channels": 2},
        ))
        connections.append(Connection(PortRef.parse(previous), PortRef.parse(f"{node_id}:in")))
        previous = f"{node_id}:out"
    nodes.append(Node(id="sink", component="orpheus.builtin.null_sink", params={"channels": 2}))
    connections.append(Connection(PortRef.parse(previous), PortRef.parse("sink:in")))
    project.graph = Graph(nodes={node.id: node for node in nodes}, connections=connections)
    return project


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", type=int, default=128, help="Number of gain nodes")
    parser.add_argument("--blocks", type=int, default=1000, help="Blocks to process")
    parser.add_argument("--block-size", type=int, default=128)
    args = parser.parse_args()

    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    project = make_project(args.nodes, args.block_size)

    started = time.perf_counter()
    plan = GraphCompiler(registry).compile(project)
    compile_seconds = time.perf_counter() - started

    runtime = ROOT / "build" / ("orpheus_runtime.exe" if __import__("os").name == "nt" else "orpheus_runtime")
    if not runtime.exists():
        raise SystemExit(f"runtime not built: {runtime}")

    with tempfile.TemporaryDirectory(prefix="orpheus-benchmark-") as temp:
        temp_path = Path(temp)
        plan_path = temp_path / "plan.json"
        plan_path.write_text(json.dumps(asdict(plan), ensure_ascii=False), encoding="utf-8")
        started = time.perf_counter()
        result = subprocess.run(
            [
                str(runtime),
                str(plan_path),
                str(ROOT / "build" / "components"),
                "--benchmark-task", "default", str(args.blocks),
            ],
            cwd=temp_path,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        run_seconds = time.perf_counter() - started
        if result.returncode != 0:
            raise SystemExit(result.stderr or result.stdout)

    audio_seconds = args.blocks * args.block_size / project.sample_rate
    latency = {
        key: float(value)
        for key, value in re.findall(r"(p50_us|p95_us|p99_us|max_us)=([0-9.]+)", result.stdout)
    }
    deadline_us = args.block_size * 1_000_000 / project.sample_rate
    report = {
        "gain_nodes": args.nodes,
        "total_nodes": len(plan.nodes),
        "blocks": args.blocks,
        "block_size": args.block_size,
        "compile_ms": round(compile_seconds * 1000, 3),
        "run_ms": round(run_seconds * 1000, 3),
        "blocks_per_second": round(args.blocks / run_seconds, 3),
        "realtime_factor": round(audio_seconds / run_seconds, 3),
        "deadline_us": round(deadline_us, 3),
        **latency,
        "p99_deadline_percent": round(latency.get("p99_us", 0.0) * 100 / deadline_us, 3),
    }
    print(json.dumps(report, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
