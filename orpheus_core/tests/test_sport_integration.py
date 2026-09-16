from __future__ import annotations

import json
from pathlib import Path

import yaml

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import ProjectLoader, project_to_dict
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]


def _registry() -> Registry:
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    return registry


def _stream(resource: str, first_channel: int) -> dict:
    return {
        "resource": resource,
        "slots": list(range(16)),
        "channels": list(range(first_channel, first_channel + 16)),
        "slot_count": 16,
        "format": "q1_31",
    }


def _project_document() -> dict:
    return {
        "version": "0.1.0",
        "target": "adsp21593",
        "sample_rate": 48000,
        "block_size": 32,
        "clock_domains": [
            {"id": "audio48", "sample_rate": 48000, "assurance": "user_guaranteed"}
        ],
        "trigger_groups": [
            {
                "id": "a2b_block",
                "clock_domain": "audio48",
                "dispatch": "master",
                "master": "sport0a_rx",
                "members": ["sport0a_rx", "sport0b_rx"],
            }
        ],
        "tasks": [
            {
                "id": "audio",
                "sample_rate": 48000,
                "block_size": 32,
                "clock_domain": "audio48",
                "trigger_group": "a2b_block",
            }
        ],
        "sport_bindings": [
            {"node": "sport_in", "streams": [_stream("sport0b_rx", 0), _stream("sport0a_rx", 16)]},
            {"node": "sport_out", "streams": [_stream("sport2a_tx", 0), _stream("sport2b_tx", 16)]},
        ],
        "graph": {
            "nodes": [
                {
                    "id": "sport_in",
                    "component": "orpheus.builtin.sport_tdm_in",
                    "task": "audio",
                    "params": {"channels": 32, "sample_rate": 48000, "clock_domain": "audio48"},
                },
                {
                    "id": "gain",
                    "component": "orpheus.builtin.gain",
                    "task": "audio",
                    "params": {"channels": 32, "gain_db": -6.0, "smoothing_ms": 0.0},
                },
                {
                    "id": "sport_out",
                    "component": "orpheus.builtin.sport_tdm_out",
                    "task": "audio",
                    "params": {"channels": 32, "sample_rate": 48000, "clock_domain": "audio48"},
                },
            ],
            "connections": [
                {"from": "sport_in:out", "to": "gain:in"},
                {"from": "gain:out", "to": "sport_out:in"},
            ],
        },
    }


def test_sport_contract_roundtrip_compile_and_generate(tmp_path: Path) -> None:
    project_path = tmp_path / "sport.yaml"
    project_path.write_text(yaml.safe_dump(_project_document(), sort_keys=False), encoding="utf-8")
    project = ProjectLoader().load(project_path)
    assert project_to_dict(project)["trigger_groups"][0]["master"] == "sport0a_rx"

    registry = _registry()
    plan = GraphCompiler(registry).compile(project, target="adsp21593")
    assert plan.target == "adsp21593"
    assert plan.tasks[0]["clock_domain"] == "audio48"
    assert plan.tasks[0]["trigger_group"] == "a2b_block"
    assert len(plan.sport_bindings) == 2

    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    header = (generated / "include" / "orpheus_sport.h").read_text(encoding="utf-8")
    source = (generated / "src" / "orpheus_sport.c").read_text(encoding="utf-8")
    graph = (generated / "src" / "orpheus_graph.c").read_text(encoding="utf-8")
    manifest = json.loads((generated / "orpheus_app_manifest.json").read_text(encoding="utf-8"))
    gain_source = (generated / "components" / "orpheus_builtin_gain" / "src" / "gain.c").read_text(encoding="utf-8")
    abi_header = (generated / "include" / "orpheus_abi.h").read_text(encoding="utf-8")

    assert "const int32_t* sport0a_rx" in header
    assert "int32_t* sport2b_tx" in header
    assert "orpheus_graph_process_task_audio_sport" in source
    assert "frame * 16u + 15u" in source
    assert "orpheus_generated_process_task_audio_at" in graph
    assert "sequence_errors" in graph
    assert manifest["clock_domains"][0]["id"] == "audio48"
    assert manifest["sport_bindings"][0]["streams"][0]["resource"] == "sport0b_rx"
    assert gain_source.startswith(
        "#define ORPHEUS_ENTRY_NAME orpheus_builtin_gain_get_interface"
    )
    assert not (generated / "src" / "host_cli.c").exists()
    assert not (generated / "src" / "bridge_tcp.c").exists()
    assert abi_header.startswith("#ifndef ORPHEUS_API\n#define ORPHEUS_API")


def test_sport_s24_left_format_is_generated(tmp_path: Path) -> None:
    document = _project_document()
    for binding in document["sport_bindings"]:
        for stream in binding["streams"]:
            stream["format"] = "s24_left_in_s32"

    path = tmp_path / "sport-s24-left.yaml"
    path.write_text(yaml.safe_dump(document, sort_keys=False), encoding="utf-8")
    plan = GraphCompiler(_registry()).compile(
        ProjectLoader().load(path), target="adsp21593"
    )
    generated = tmp_path / "generated"
    CodeGenerator(_registry(), ROOT).generate(plan, generated)

    source = (generated / "src" / "orpheus_sport.c").read_text(encoding="utf-8")
    assert "static float orpheus_decode_s24_left" in source
    assert "static int32_t orpheus_encode_s24_left" in source
    assert "orpheus_decode_s24_left(io->sport0b_rx[" in source
    assert "orpheus_encode_s24_left(g_sport_out_sport_out[" in source


def test_independent_clock_domains_keep_independent_task_rates(tmp_path: Path) -> None:
    document = _project_document()
    document["clock_domains"].append(
        {"id": "voice44", "sample_rate": 44100, "assurance": "user_guaranteed"}
    )
    document["trigger_groups"].append(
        {
            "id": "voice_block",
            "clock_domain": "voice44",
            "dispatch": "caller",
            "members": ["sport4a_rx"],
        }
    )
    document["tasks"].append(
        {
            "id": "voice",
            "sample_rate": 44100,
            "block_size": 16,
            "clock_domain": "voice44",
            "trigger_group": "voice_block",
        }
    )
    document["graph"]["nodes"].extend([
        {
            "id": "voice_in",
            "component": "orpheus.builtin.sport_tdm_in",
            "task": "voice",
            "params": {"channels": 1, "sample_rate": 44100, "clock_domain": "voice44"},
        },
        {
            "id": "voice_out",
            "component": "orpheus.builtin.sport_tdm_out",
            "task": "voice",
            "params": {"channels": 1, "sample_rate": 44100, "clock_domain": "voice44"},
        },
    ])
    document["graph"]["connections"].append(
        {"from": "voice_in:out", "to": "voice_out:in"}
    )
    document["sport_bindings"].extend([
        {
            "node": "voice_in",
            "streams": [{"resource": "sport4a_rx", "slots": [0], "channels": [0], "slot_count": 2, "format": "q1_31"}],
        },
        {
            "node": "voice_out",
            "streams": [{"resource": "sport4a_tx", "slots": [0], "channels": [0], "slot_count": 2, "format": "q1_31"}],
        },
    ])
    path = tmp_path / "multi-clock.yaml"
    path.write_text(yaml.safe_dump(document, sort_keys=False), encoding="utf-8")
    plan = GraphCompiler(_registry()).compile(ProjectLoader().load(path), target="adsp21593")
    rates = {task["id"]: task["sample_rate"] for task in plan.tasks}
    assert rates == {"audio": 48000, "voice": 44100}
    assert plan.schedule["multi_clock"] is True
