"""参数查表每次真实到达的插值、音频位型、只读结果与历史四入口验收。"""

import json

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_channel_router_interfaces import CAPTURE, SOURCE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


@pytest.mark.parametrize("channels,axis,table,requests", [
    (1, [0.0], [-3.0], [-10.0, 10.0]),
    (3, [0.0, 10.0, 20.0], [0.0, 5.0, 10.0], [5.0, 15.0]),
    (7, [-3.0, -.25, 1.5, 4.0], [.75, -.5, 2.25, -1.0], [-10.0, 10.0]),
    (32, [i / 8 for i in range(256)], [f32((i % 13 - 6) / 17) for i in range(256)], [.37, 30.13]),
])
def test_lookup_four_entries_preserve_each_arrival_and_history(tmp_path, channels, axis, table, requests):
    results = []
    for request in requests:
        x = f32(request)
        if x <= axis[0]:
            value = table[0]
        elif x >= axis[-1]:
            value = table[-1]
        else:
            i = next(i for i in range(len(axis) - 1) if axis[i] <= x <= axis[i + 1])
            fraction = f32(f32(x - axis[i]) / f32(axis[i + 1] - axis[i]))
            value = f32(table[i] + f32(fraction * f32(table[i + 1] - table[i])))
        results.append(value)
    history = ["[" + ",".join(format(value, ".4g") for value in values) + "]"
               for values in ([results[0]] * 2, [results[0]] * 2 + [results[1]] * 2)]
    source = create_component(tmp_path, "router_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "ticks": scalar("int", 0),
    }, SOURCE, source=True)
    capture = create_component(tmp_path, "router_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@MAP@", ",".join(map(str, range(channels))))
        .replace("@INPUT@", str(channels)).replace("@COUNT@", str(channels)))
    lookup = ROOT / "components" / "orpheus" / "builtin" / "interp_lut" / "component.yaml"
    manifests = [source, lookup, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.router_source"),
        "lookup": Node("lookup", "orpheus.builtin.interp_lut", params={
            "channels": channels, "x_axis": ",".join(map(str, axis)), "y_axis": ",".join(map(str, table)),
            "x": requests[0],
        }),
        "capture": Node("capture", "orpheus.test.router_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("lookup", "in")),
        Connection(PortRef("lookup", "out"), PortRef("capture", "in")),
    ]))
    update = r'''
        if(tick) {
            OrpheusValue value={ORPHEUS_VALUE_FLOAT};value.value.f32=@REQUEST@f;
#ifdef DYNAMIC
            if(runtime.set_parameter("lookup","x",value)) return 10;
#else
            if(orpheus_control_set_value("lookup","x",value)) return 10;
#endif
        }
'''
    verify = r'''
        static const uint32_t expected[]={@RESULTS@};
        static const char* histories[]={@HISTORY@};
        OrpheusValue value={ORPHEUS_VALUE_FLOAT},history={ORPHEUS_VALUE_STRING};
#ifdef DYNAMIC
        if(runtime.get_parameter("lookup","y",&value) || runtime.get_parameter("lookup","history",&history)) return 11;
#else
        if(orpheus_control_get_value("lookup","y",&value) || orpheus_control_get_value("lookup","history",&history)) return 11;
#endif
        uint32_t actual;memcpy(&actual,&value.value.f32,4);
        if(value.type!=ORPHEUS_VALUE_FLOAT || actual!=expected[tick] ||
            history.type!=ORPHEUS_VALUE_STRING || strcmp(history.value.str,histories[tick])) return 12;
'''
    harness = ("#include <string.h>\n" + four_entry_harness()).replace(
        "for(int tick=0;tick<2;tick++) {",
        "for(int tick=0;tick<2;tick++) {" + update.replace("@REQUEST@", repr(f32(requests[1]))),
    ).replace("        printf(", verify.replace("@RESULTS@", ",".join(str(bits(v)) for v in results))
              .replace("@HISTORY@", ",".join(json.dumps(value) for value in history)) + "        printf(")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, 8]])
