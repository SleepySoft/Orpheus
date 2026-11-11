"""字符串探针必须经组件getter；名称、ID与枚举读取都传递实时结果和错误。"""
import yaml

from orpheus_core.compiler import GraphCompiler
from orpheus_core.component_codegen import generate_component
from orpheus_core.project import Graph, Node, Project
from orpheus_core.registry import Registry
from test_interface_delivery import compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, four_entry_harness


ALGORITHM = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_string_probe_algorithm(orpheus_test_string_probe_State* s,
    orpheus_test_string_probe_IO* io,const OrpheusProcessContext* ctx) {
    (void)io;
    s->calls++;s->last=(float)s->calls;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
int orpheus_test_string_probe_get_parameter(orpheus_test_string_probe_State* s,
    const char* key,OrpheusValue* value) {
    if(strcmp(key,"detail")) return ORPHEUS_ERR_NOT_FOUND;
    if(s->calls==2) return ORPHEUS_ERR_PROCESSING;
    value->type=ORPHEUS_VALUE_STRING;
    value->value.str=s->calls ? "{\"calls\":1}" : "{\"calls\":0}";
    return ORPHEUS_OK;
}
'''

ACCESS = r'''
#ifdef DYNAMIC
#define NAME(v) runtime.get_parameter("capture","detail",v)
#define ID(v) runtime.read_id(@ID@,v)
#else
#define NAME(v) orpheus_control_get_value("capture","detail",v)
#define ID(v) orpheus_control_get_value_id(@ID@,v)
#endif
'''

CHECK = r'''
        OrpheusValue detail={ORPHEUS_VALUE_STRING,{0}};
        int expected=tick ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK;
        if(NAME(&detail)!=expected || (!expected && strcmp(detail.value.str,"{\"calls\":1}"))) return 8;
        if(ID(&detail)!=expected || (!expected && strcmp(detail.value.str,"{\"calls\":1}"))) return 9;
#ifndef DYNAMIC
        unsigned seen=0;
        for(size_t i=0;i<orpheus_control_probe_count();i++) {
            const char* node=NULL;const char* key=NULL;
            if(orpheus_control_probe_get(i,&node,&key,NULL)) return 13;
            int rc=orpheus_control_probe_get(i,&node,&key,&detail);
            if(!strcmp(node,"capture") && !strcmp(key,"detail")) {
                if(rc!=expected || (!expected && strcmp(detail.value.str,"{\"calls\":1}"))) return 10;
                seen++;
            } else if(rc) return 11;
        }
        if(seen!=1) return 12;
#endif
'''


def test_generated_string_probe_uses_getter_and_propagates_errors(tmp_path):
    manifest = create_component(tmp_path, "string_probe", {
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
        "detail": {"type": "string", "default": "{}", "capacity": 64,
                   "kind": "probe", "access": "read", "persistent": False, "readback": True},
    }, ALGORITHM, source=True)
    definition = yaml.safe_load(manifest.read_text(encoding="utf-8"))
    definition["algorithm"] = {"lifecycle": ["get_parameter"]}
    manifest.write_text(yaml.safe_dump(definition, sort_keys=False), encoding="utf-8")
    generate_component(manifest)
    registry = Registry()
    registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8,
                      graph=Graph(nodes={"capture": Node("capture", "orpheus.test.string_probe")}))
    plan = GraphCompiler(registry).compile(project)
    point = next(point for point in plan.id_map if point["key"] == "detail")
    harness = "#include <string.h>\n" + four_entry_harness().replace("task_source", "task_capture")
    harness = harness.replace("int main(", ACCESS.replace("@ID@", str(point["id"])) + "\nint main(")
    harness = harness.replace('        printf("%d', CHECK + '\n        printf("%d')
    case = compile_delivery_backends(tmp_path, registry, project, [manifest], harness)
    check_four_entries(case, [[0, 1, 1.0, 0], [1, 2, 2.0, 8]])
