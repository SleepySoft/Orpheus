"""双源逐次交付、整数/浮点 BULK 和四个真实执行入口的位型验收。"""

import ctypes
import struct
import subprocess

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar

BLOCKS = [2, 1, 2, 1]

SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_selector_source_algorithm(orpheus_test_selector_source_State* s,
    orpheus_test_selector_source_IO* io, const OrpheusProcessContext* ctx) {
    if(io->a.buffer) {
        for(uint32_t n=0;n<ctx->frame_count;n++)
            for(uint32_t c=0;c<io->a.buffer->channels;c++)
                io->a.samples[n*io->a.buffer->channels+c]=
                    (float)((int32_t)(((s->ticks*8+n)*97+c*577)%12001)-6000)/32768.0f;
        int rc=orpheus_test_selector_source_emit_a(ctx,&io->a);
        if(rc) return rc;
    }
    if(io->b.buffer) {
        for(uint32_t n=0;n<ctx->frame_count;n++)
            for(uint32_t c=0;c<io->b.buffer->channels;c++)
                io->b.samples[n*io->b.buffer->channels+c]=
                    (float)((int32_t)(((s->ticks*8+n)*97+(c+64)*577)%12001)-6000)/32768.0f;
        int rc=orpheus_test_selector_source_emit_b(ctx,&io->b);
        if(rc) return rc;
    }
    s->ticks++;
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_selector_capture_@NAME@_algorithm(orpheus_test_selector_capture_@NAME@_State* s,
    orpheus_test_selector_capture_@NAME@_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[][@SAMPLES@]={@EXPECTED@};
    static const uint64_t frames[]={@FRAMES@};
    if(s->calls>=@CALLS@ || ctx->frame_count!=8 || ctx->valid_frames!=8 || ctx->epoch!=1
        || ctx->frame_index!=frames[s->calls] || io->in.buffer->frame_count!=8)
        return ORPHEUS_ERR_INVALID_ARG;
    if(memcmp(io->in.samples,expected[s->calls],sizeof(expected[0]))) return ORPHEUS_ERR_PROCESSING;
    s->calls++;
    return ORPHEUS_OK;
}
'''


def expected_deliveries(params):
    count, base = params["channels_out"], params.get("index_base", 0)
    initial = ([int(v) for v in params["indices"].split(",")]
               if params.get("indices") else list(range(base, base + count)))
    gains = ([float(v) for v in params["gains"].split(",")]
             if params.get("gains") else [1.0] * count)
    latest = {"a": -1, "b": -1, "base": -1}
    deliveries, positions, tick = [], [], 0

    def sample(side, channel, frame):
        source = "a" if side == "base" else side
        t = latest[side]
        if t < 0:
            return 0.0
        return (((t * 8 + frame) * 97 + (channel + (64 if source == "b" else 0)) * 577)
                % 12001 - 6000) / 32768

    for step, blocks in enumerate(BLOCKS):
        indices = ([v if v == 2147483647 else v + 1 for v in initial] if step == 1
                   else [-1] * count if step == 2 and not params.get("accumulate") else initial)
        vector = ([(c % 5 - 2) / 4 for c in range(count)] if step == 1
                  else [0.0] * count if step == 2 else gains)
        for _ in range(blocks):
            for arrival in ("a", "base", "b"):
                if ((arrival == "base" and not params.get("accumulate"))
                        or not params.get("_connect_" + arrival, True)):
                    continue
                latest[arrival] = tick
                values = []
                for n in range(8):
                    for c, selected in enumerate(indices):
                        original = sample("base", c, n) if params.get("accumulate") else 0.0
                        index = selected - base
                        total = params["channels_a"] + params["channels_b"]
                        if index >= total and params.get("high_index_policy", 0):
                            index = total - 1
                        side = "a" if index < params["channels_a"] else "b"
                        channel = index if side == "a" else index - params["channels_a"]
                        if (step == 2 or index < 0 or index >= total
                                or not params.get("_connect_" + side, True)):
                            value = original
                        else:
                            value = sample(side, channel, n)
                            if params.get("apply_gain"):
                                value = ctypes.c_float(value * vector[c]).value
                            if params.get("accumulate"):
                                value = ctypes.c_float(original + value).value
                        values.append(struct.unpack("<I", struct.pack("<f", value))[0])
                deliveries.append(values)
                positions.append(tick * 8)
            tick += 1
    return deliveries, positions


def verify_channel_selectors(tmp_path, cases):
    widths = {side: max(p["channels_" + side] for p in cases.values()) for side in ("a", "b")}
    source = create_component(tmp_path, "selector_source", {
        "a": audio("output", channels=widths["a"], drive_port_no=1),
        "b": audio("output", channels=widths["b"], drive_port_no=2),
        "ticks": scalar("int", 0),
    }, SOURCE, source=True)
    manifests = [source, *[
        ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
        for name in ("channel_selector", "channel_router")]]
    nodes, edges = {"source": Node("source", "orpheus.test.selector_source")}, []
    for name, params in cases.items():
        nodes[name] = Node(name, "orpheus.builtin.channel_selector",
                           params={k: v for k, v in params.items() if not k.startswith("_")})
        for side in ("a", "base", "b"):
            if ((side == "base" and not params.get("accumulate"))
                    or not params.get("_connect_" + side, True)):
                continue
            route, origin = name + "_" + side, "a" if side == "base" else side
            count = params["channels_out"] if side == "base" else params["channels_" + side]
            nodes[route] = Node(route, "orpheus.builtin.channel_router",
                                params={"channels_in": widths[origin], "channels_out": count})
            edges.extend([
                Connection(PortRef("source", origin), PortRef(route, "in")),
                Connection(PortRef(route, "out"), PortRef(name, side)),
            ])
        expected, frames = expected_deliveries(params)
        capture = create_component(tmp_path, "selector_capture_" + name, {
            "in": audio("input", channels=params["channels_out"], process=True),
            "calls": scalar("int", 0), "expected_calls": scalar("int", len(expected)),
        }, CAPTURE.replace("@NAME@", name).replace("@SAMPLES@", str(8 * params["channels_out"]))
            .replace("@EXPECTED@", ",".join("{" + ",".join(map(str, row)) + "}" for row in expected))
            .replace("@FRAMES@", ",".join(map(str, frames))).replace("@CALLS@", str(len(expected))))
        manifests.append(capture)
        nodes[name + "_capture"] = Node(name + "_capture", "orpheus.test.selector_capture_" + name)
        edges.append(Connection(PortRef(name, "out"), PortRef(name + "_capture", "in")))
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes=nodes, connections=edges))
    harness = (ROOT / "tests" / "channel_selector_runner.cpp").read_text(encoding="utf-8")
    directory, plan_path, plan, *_ = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert not plan.warnings
    args = []
    for name, params in cases.items():
        args.append(name)
        for key, value_type in (("indices", "int"), ("gains", "float")):
            slots = [e for e in plan.id_map if e["node"] == name and e["key"] == key]
            assert len(slots) == 1
            slot = slots[0]
            assert (slot["form"], slot["count"], slot["type"]) == ("BULK", params["channels_out"], value_type)
            args.append(str(slot["id"]))
    for mode in ("global", "task"):
        commands = [
            [str(directory / "build" / "dynamic.exe"), str(plan_path), str(directory / "build" / "components"), mode, *args],
            [str(directory / "build" / "generated_backend.exe"), mode, *args],
        ]
        for command in commands:
            result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, (mode, command, result.stdout, result.stderr)


def test_channel_selector_four_entries(tmp_path):
    mapping = [-2147483648, -1, 0, 1, 21, 22, 23, 70, 71, 72, 2147483647]
    verify_channel_selectors(tmp_path, {
        "main": {"channels_a": 22, "channels_b": 49, "channels_out": 22, "index_base": 1,
                 "high_index_policy": 1, "apply_gain": True, "indices": ",".join(map(str, mapping * 2)),
                 "gains": ",".join(str((c % 7 - 3) / 4) for c in range(22))},
        "copy": {"channels_a": 64, "channels_b": 64, "channels_out": 64, "index_base": 0,
                 "indices": ",".join(map(str, [0, 63, 64, 127, 128, -1, 2147483647, -2147483648] * 8))},
        "missing": {"channels_a": 17, "channels_b": 32, "channels_out": 6, "index_base": 1,
                    "indices": "0,1,17,18,49,50", "apply_gain": True, "_connect_b": False},
        "add": {"channels_a": 17, "channels_b": 1, "channels_out": 22, "index_base": 1,
                "indices": ",".join(map(str, mapping * 2)), "apply_gain": True, "accumulate": True, "_connect_b": False},
    })


def test_channel_selector_default_mapping_and_missing_base(tmp_path):
    verify_channel_selectors(tmp_path, {
        "one": {"channels_a": 1, "channels_b": 1, "channels_out": 1},
        "defaults": {"channels_a": 3, "channels_b": 2, "channels_out": 6, "index_base": 1},
        "nobase": {"channels_a": 3, "channels_b": 2, "channels_out": 6, "accumulate": True, "_connect_base": False},
        "bonly": {"channels_a": 3, "channels_b": 2, "channels_out": 6, "_connect_a": False},
    })
