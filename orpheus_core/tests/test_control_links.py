"""控制链路（control_connections）测试：schema / 编译校验 / 工程加载往返。

覆盖 design_control_link_eval 第 0/1 期核心框架：
- manifest 新字段（bindable / control_source / shape）经 schema 校验；
- 合法标量链（level_detect.level → gain.gain_db）编译出 plan.control_links；
- 负例：目标非 bindable / affects_signature / restart_required、形状失配、类型不匹配；
- 工程 YAML 加载→保存→重载不丢 control_connections；跨子图边界报中文错误。
"""

from __future__ import annotations

import copy
from pathlib import Path

import pytest
import yaml

from orpheus_core import schemas
from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.project import (
    ControlConnection,
    Graph,
    Node,
    PortRef,
    Project,
    ProjectLoader,
    SubParameter,
    Subcomponent,
)
from orpheus_core.registry import ComponentInfo, Registry
from orpheus_core.subgraph import flatten_project

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def registry():
    reg = Registry()
    reg.add_search_path(ROOT / "components")
    reg.scan()
    return reg


@pytest.fixture()
def compiler(registry):
    return GraphCompiler(registry)


def ctl(a: str, b: str) -> ControlConnection:
    return ControlConnection(from_ref=PortRef.parse(a), to_ref=PortRef.parse(b))


def make_project(
    nodes: dict[str, Node],
    control_connections: list[ControlConnection] | None = None,
) -> Project:
    project = Project(metadata={"name": "t"})
    project.graph = Graph(nodes=nodes, connections=[])
    project.control_connections = control_connections or []
    return project


def add_stub_component(registry: Registry, comp_id: str, parameters: list[dict]) -> None:
    """注册一个纯参数 stub 组件（无端口），用于精确命中单条校验规则。"""
    manifest = {
        "id": comp_id,
        "name": comp_id,
        "version": "1.0.0",
        "abi_version": 1,
        "parameters": parameters,
    }
    schemas.validate(manifest, schemas.load_component_manifest_schema())
    registry._components[comp_id] = ComponentInfo(
        id=comp_id,
        version="1.0.0",
        abi_version=1,
        package_type="source",
        manifest_path=Path("stub"),
        root_dir=Path("."),
        manifest=manifest,
    )


# ---- schema ----


def test_manifest_schema_accepts_control_fields():
    schema = schemas.load_component_manifest_schema()
    manifest = {
        "id": "orpheus.builtin._test_ctl",
        "version": "1.0.0",
        "abi_version": 1,
        "parameters": [
            {"id": "gain", "type": "float", "bindable": True},
            {"id": "level", "type": "float", "readback": True, "control_source": True},
            {"id": "matrix", "type": "string", "shape": ["param:rows", "param:cols"]},
        ],
    }
    schemas.validate(manifest, schema)  # 不抛异常即通过

    bad = copy.deepcopy(manifest)
    bad["parameters"][0]["bindable"] = "yes"  # 类型错误
    with pytest.raises(Exception):
        schemas.validate(bad, schema)


def test_project_schema_accepts_control_connections():
    doc = {
        "version": "0.1.0",
        "graph": {"nodes": [], "connections": []},
        "control_connections": [{"from": "level1:level", "to": "gain1:gain_db"}],
    }
    schemas.validate(doc, schemas.load_project_schema())


# ---- 正向：标量控制链 ----


def test_compile_scalar_control_link(compiler):
    project = make_project(
        {
            "level1": Node(id="level1", component="orpheus.builtin.level_detect",
                           params={"channels": 2}),
            "gain1": Node(id="gain1", component="orpheus.builtin.gain",
                          params={"gain_db": 0.0, "channels": 2}),
        },
        [ctl("level1:level", "gain1:gain_db")],
    )
    plan = compiler.compile(project)
    assert plan.control_links == [
        {
            "src_node": "level1",
            "src_param": "level",
            "dst_node": "gain1",
            "dst_param": "gain_db",
            "type": "float",
            "shape": [],
            "count": 1,
        }
    ]


def test_compile_matrix_shape_link(compiler):
    """形状链路：两端都是 [2×2] 时通过，plan 记录求值后的形状与 count。"""
    project = make_project(
        {
            "m1": Node(id="m1", component="orpheus.builtin.matrix_mul",
                       params={"rows": 2, "cols": 2}),
            "m2": Node(id="m2", component="orpheus.builtin.matrix_mul",
                       params={"rows": 2, "cols": 2}),
        },
        [ctl("m1:matrix", "m2:matrix")],
    )
    # matrix 是 restart_required 且未声明 bindable/control_source：此用例仅验证
    # 形状求值器本身（直接调用内部方法），编译仍应拒绝。
    src_comp = compiler.registry.get("orpheus.builtin.matrix_mul")
    node = project.graph.nodes["m1"]
    task = project.get_default_task()
    shape = compiler._resolve_param_shape(
        next(p for p in src_comp.manifest["parameters"] if p["id"] == "matrix"),
        node, src_comp, task,
    )
    assert shape == [2, 2]
    with pytest.raises(CompileError, match="control_source"):
        compiler.compile(project)


# ---- 负例 ----


def test_reject_target_not_bindable(compiler):
    project = make_project(
        {
            "level1": Node(id="level1", component="orpheus.builtin.level_detect", params={"channels": 2}),
            "gain1": Node(id="gain1", component="orpheus.builtin.gain", params={"channels": 2}),
        },
        [ctl("level1:level", "gain1:smoothing_ms")],  # smoothing_ms 未声明 bindable
    )
    with pytest.raises(CompileError, match="不允许绑定"):
        compiler.compile(project)


def test_reject_target_affects_signature(compiler, registry):
    add_stub_component(registry, "orpheus.builtin._test_sig", [
        {"id": "size", "type": "float", "bindable": True,
         "update_policy": "immediate", "affects_signature": True},
    ])
    project = make_project(
        {
            "level1": Node(id="level1", component="orpheus.builtin.level_detect", params={"channels": 2}),
            "s1": Node(id="s1", component="orpheus.builtin._test_sig"),
        },
        [ctl("level1:level", "s1:size")],
    )
    with pytest.raises(CompileError, match="affects_signature"):
        compiler.compile(project)


def test_reject_target_restart_required(compiler, registry):
    add_stub_component(registry, "orpheus.builtin._test_restart", [
        {"id": "coef", "type": "float", "bindable": True,
         "update_policy": "restart_required"},
    ])
    project = make_project(
        {
            "level1": Node(id="level1", component="orpheus.builtin.level_detect", params={"channels": 2}),
            "r1": Node(id="r1", component="orpheus.builtin._test_restart"),
        },
        [ctl("level1:level", "r1:coef")],
    )
    with pytest.raises(CompileError, match="更新策略"):
        compiler.compile(project)


def test_reject_shape_mismatch(compiler, registry):
    add_stub_component(registry, "orpheus.builtin._test_mat", [
        {"id": "rows", "type": "int", "default": 2},
        {"id": "cols", "type": "int", "default": 2},
        {"id": "matrix", "type": "float", "bindable": True,
         "update_policy": "immediate", "shape": ["param:rows", "param:cols"]},
    ])
    project = make_project(
        {
            "probe1": Node(id="probe1", component="orpheus.builtin.probe_rms", params={"channels": 2}),
            "m1": Node(id="m1", component="orpheus.builtin._test_mat"),
        },
        [ctl("probe1:rms", "m1:matrix")],  # 标量 → [2×2]
    )
    with pytest.raises(CompileError, match="形状不匹配"):
        compiler.compile(project)


def test_reject_type_mismatch(compiler, registry):
    add_stub_component(registry, "orpheus.builtin._test_intsrc", [
        {"id": "count", "type": "int", "readback": True, "control_source": True},
    ])
    project = make_project(
        {
            "s1": Node(id="s1", component="orpheus.builtin._test_intsrc"),
            "gain1": Node(id="gain1", component="orpheus.builtin.gain", params={"channels": 2}),
        },
        [ctl("s1:count", "gain1:gain_db")],  # int → float，禁止隐式转换
    )
    with pytest.raises(CompileError, match="类型不匹配"):
        compiler.compile(project)


def test_reject_source_not_control_source(compiler):
    project = make_project(
        {
            "gain1": Node(id="gain1", component="orpheus.builtin.gain", params={"channels": 2}),
            "gain2": Node(id="gain2", component="orpheus.builtin.gain", params={"channels": 2}),
        },
        [ctl("gain1:gain_db", "gain2:gain_db")],  # gain_db 未声明 control_source
    )
    with pytest.raises(CompileError, match="control_source"):
        compiler.compile(project)


# ---- 工程加载 / 保存往返 ----


def test_control_connections_survive_load_save_compile(compiler, tmp_path):
    doc = {
        "version": "0.1.0",
        "metadata": {"name": "ctl"},
        "graph": {
            "nodes": [
                {"id": "level1", "component": "orpheus.builtin.level_detect", "params": {"channels": 2}},
                {"id": "gain1", "component": "orpheus.builtin.gain", "params": {"channels": 2}},
            ],
            "connections": [],
        },
        "control_connections": [{"from": "level1:level", "to": "gain1:gain_db"}],
    }
    path = tmp_path / "project.yaml"
    path.write_text(yaml.safe_dump(doc, allow_unicode=True), encoding="utf-8")

    loader = ProjectLoader()
    project = loader.load(path)
    assert len(project.control_connections) == 1
    assert str(project.control_connections[0].from_ref) == "level1:level"
    assert str(project.control_connections[0].to_ref) == "gain1:gain_db"

    # 保存 → 重载不丢
    loader.save(project, path)
    reloaded = loader.load(path)
    assert len(reloaded.control_connections) == 1
    assert str(reloaded.control_connections[0].from_ref) == "level1:level"

    # 加载 → 展开 → 编译产出 control_links
    plan = compiler.compile(flatten_project(reloaded))
    assert len(plan.control_links) == 1
    assert plan.control_links[0]["src_node"] == "level1"
    assert plan.control_links[0]["dst_param"] == "gain_db"


def test_public_parameters_survive_load_save(tmp_path):
    doc = {
        "version": "0.1.0",
        "graph": {"nodes": [], "connections": []},
        "subcomponents": [{
            "id": "chain",
            "ports": [],
            "public_parameters": [{
                "id": "gain", "name": "增益", "direction": "input",
                "maps_to": "g:gain_db", "type": "float", "default": -6.0,
                "shape": [], "update_policy": "smoothed",
            }],
            "graph": {
                "nodes": [{"id": "g", "component": "orpheus.builtin.gain", "params": {"channels": 1}}],
                "connections": [],
            },
        }],
    }
    path = tmp_path / "project.yaml"
    path.write_text(yaml.safe_dump(doc, allow_unicode=True), encoding="utf-8")
    loader = ProjectLoader()
    project = loader.load(path)
    parameter = project.subcomponents[0].public_parameters[0]
    assert parameter.maps_to == "g:gain_db"
    assert parameter.default == -6.0
    assert parameter.update_policy == "smoothed"
    loader.save(project, path)
    saved = yaml.safe_load(path.read_text(encoding="utf-8"))
    saved_parameter = saved["subcomponents"][0]["public_parameters"][0]
    assert saved_parameter == {
        key: value
        for key, value in doc["subcomponents"][0]["public_parameters"][0].items()
        if key != "shape"
    }


def test_control_connection_across_subgraph_maps_public_parameters(compiler):
    sub = Subcomponent(
        id="chain",
        graph=Graph(
            nodes={
                "level": Node(id="level", component="orpheus.builtin.level_detect", params={"channels": 2}),
                "g": Node(id="g", component="orpheus.builtin.gain", params={"channels": 2}),
            },
            connections=[],
        ),
        public_parameters=[
            SubParameter(id="level", direction="output", maps_to="level:level"),
            SubParameter(id="gain", direction="input", maps_to="g:gain_db", default=-6.0),
        ],
    )
    project = make_project(
        {
            "sub1": Node(id="sub1", component="sub:chain", params={"gain": -12.0}),
        },
        [ctl("sub1:level", "sub1:gain")],
    )
    project.subcomponents = [sub]
    flat = flatten_project(project)
    assert flat.graph.nodes["sub1__g"].params["gain_db"] == -12.0
    assert str(flat.control_connections[0].from_ref) == "sub1__level:level"
    assert str(flat.control_connections[0].to_ref) == "sub1__g:gain_db"
    plan = compiler.compile(flat)
    assert plan.control_links[0]["src_node"] == "sub1__level"
    assert plan.control_links[0]["dst_node"] == "sub1__g"


def test_public_parameter_direction_is_enforced():
    sub = Subcomponent(
        id="chain",
        graph=Graph(nodes={
            "g": Node(id="g", component="orpheus.builtin.gain", params={"channels": 1}),
        }),
        public_parameters=[
            SubParameter(id="gain", direction="input", maps_to="g:gain_db"),
        ],
    )
    project = make_project({"sub1": Node(id="sub1", component="sub:chain")}, [ctl("sub1:gain", "sub1:gain")])
    project.subcomponents = [sub]
    with pytest.raises(CompileError, match="方向应为 output"):
        flatten_project(project)


# ---- 生成路径（代码生成） ----


def test_codegen_control_tick_emission(compiler, registry, tmp_path):
    """生成路径：标量链产出两相快照 control_tick；数组链只生成注释说明、不产生读写。"""
    add_stub_component(registry, "orpheus.builtin._test_arrsrc", [
        {"id": "rows", "type": "int", "default": 2},
        {"id": "vec", "type": "float", "readback": True, "control_source": True,
         "shape": ["param:rows"]},
    ])
    add_stub_component(registry, "orpheus.builtin._test_arrdst", [
        {"id": "n", "type": "int", "default": 2},
        {"id": "vec", "type": "float", "bindable": True, "update_policy": "immediate",
         "shape": ["param:n"]},
    ])
    project = make_project(
        {
            "lvl": Node(id="lvl", component="orpheus.builtin.level_detect",
                        params={"channels": 2}),
            "g": Node(id="g", component="orpheus.builtin.gain",
                      params={"channels": 2}),
            "asrc": Node(id="asrc", component="orpheus.builtin._test_arrsrc"),
            "adst": Node(id="adst", component="orpheus.builtin._test_arrdst"),
        },
        [ctl("lvl:level", "g:gain_db"), ctl("asrc:vec", "adst:vec")],
    )
    plan = compiler.compile(project)
    assert len(plan.control_links) == 2

    from orpheus_core.generator import CodeGenerator

    CodeGenerator(registry, ROOT).generate(plan, tmp_path / "gen")
    main_c = (tmp_path / "gen" / "src" / "main.c").read_text(encoding="utf-8")

    # 两相快照函数 + 块末尾挂载
    assert "static void control_tick(void)" in main_c
    assert "control_tick();  /* 控制链路" in main_c
    # 标量链：经组件 ABI get/set_parameter，注释带形状标注
    assert "/* lvl.level [标量] -> g.gain_db */" in main_c
    assert 'g_iface_lvl->get_parameter(g_state_lvl, "level", &g_ctl_snap[0])' in main_c
    assert 'g_iface_g->set_parameter(g_state_g, "gain_db", &g_ctl_snap[0])' in main_c
    # 数组链（count=2）：仅注释说明本期不执行，不产生读写调用
    assert "asrc.vec [2] -> adst.vec：数组链（count=2）本期不执行" in main_c
    assert "g_iface_asrc->get_parameter" not in main_c
    assert "g_iface_adst->set_parameter" not in main_c


# ---- 重复目标 / 扇出 / 多跳 ----


def test_reject_duplicate_control_target(compiler):
    """同一目标参数被两条控制链驱动：两相快照下同块两写是模糊行为，编译拒绝。"""
    project = make_project(
        {
            "lvl": Node(id="lvl", component="orpheus.builtin.level_detect",
                        params={"channels": 2}),
            "m": Node(id="m", component="orpheus.builtin.probe_rms",
                      params={"channels": 2}),
            "g": Node(id="g", component="orpheus.builtin.gain",
                      params={"channels": 2}),
        },
        [ctl("lvl:level", "g:gain_db"), ctl("m:rms", "g:gain_db")],
    )
    with pytest.raises(CompileError, match="控制连接目标重复"):
        compiler.compile(project)


def test_control_fanout(compiler):
    """一个 control_source 扇出到两个 bindable 目标：编译通过，plan 按声明顺序产出两条链。"""
    project = make_project(
        {
            "lvl": Node(id="lvl", component="orpheus.builtin.level_detect",
                        params={"channels": 2}),
            "g1": Node(id="g1", component="orpheus.builtin.gain",
                       params={"channels": 2}),
            "g2": Node(id="g2", component="orpheus.builtin.gain",
                       params={"channels": 2}),
        },
        [ctl("lvl:level", "g1:gain_db"), ctl("lvl:level", "g2:gain_db")],
    )
    plan = compiler.compile(project)
    assert [(l["src_node"], l["dst_node"]) for l in plan.control_links] == [
        ("lvl", "g1"),
        ("lvl", "g2"),
    ]


def test_control_multi_hop(compiler, registry):
    """多跳中继链 A→B→C→D：中继参数同时声明 bindable+control_source（readback 可读）。

    现有组件没有 bindable 兼 control_source 的参数，用 stub 中继组件验证
    lvl.level → r1.val → r2.val → g.gain_db 产出 3 条链（两相快照下每跳 1 块延迟）。
    """
    relay_param = {"id": "val", "type": "float", "readback": True,
                   "control_source": True, "bindable": True,
                   "update_policy": "immediate"}
    add_stub_component(registry, "orpheus.builtin._test_relay", [relay_param])
    project = make_project(
        {
            "lvl": Node(id="lvl", component="orpheus.builtin.level_detect",
                        params={"channels": 2}),
            "r1": Node(id="r1", component="orpheus.builtin._test_relay"),
            "r2": Node(id="r2", component="orpheus.builtin._test_relay"),
            "g": Node(id="g", component="orpheus.builtin.gain",
                      params={"channels": 2}),
        },
        [ctl("lvl:level", "r1:val"), ctl("r1:val", "r2:val"), ctl("r2:val", "g:gain_db")],
    )
    plan = compiler.compile(project)
    assert [(l["src_node"], l["src_param"], l["dst_node"], l["dst_param"])
            for l in plan.control_links] == [
        ("lvl", "level", "r1", "val"),
        ("r1", "val", "r2", "val"),
        ("r2", "val", "g", "gain_db"),
    ]
