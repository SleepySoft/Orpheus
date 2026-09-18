"""Standalone project validation used by CLI and AI file-generation workflows."""

from __future__ import annotations

import jsonschema
import yaml
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

from orpheus_core import schemas
from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.project import Project, ProjectLoader
from orpheus_core.registry import Registry
from orpheus_core.subgraph import flatten_project


@dataclass
class ValidationIssue:
    """One user-facing validation finding."""

    stage: str
    message: str
    severity: str = "error"
    json_path: str = "$"


@dataclass
class ProjectValidationReport:
    """Aggregated result for a single project YAML file."""

    path: Path
    issues: list[ValidationIssue] = field(default_factory=list)
    stages: dict[str, str] = field(default_factory=dict)
    summary: dict[str, Any] = field(default_factory=dict)

    @property
    def valid(self) -> bool:
        return not any(issue.severity == "error" for issue in self.issues)

    def add(self, issue: ValidationIssue) -> None:
        self.issues.append(issue)

    def add_error(self, stage: str, message: str, json_path: str = "$") -> None:
        self.add(ValidationIssue(stage=stage, message=message, json_path=json_path))

    def add_warning(self, stage: str, message: str, json_path: str = "$") -> None:
        self.add(
            ValidationIssue(
                stage=stage, message=message, severity="warning", json_path=json_path
            )
        )

    def _set_stage(self, stage: str, status: str) -> None:
        self.stages[stage] = status

    def to_dict(self) -> dict[str, Any]:
        return {
            "path": self.path.as_posix(),
            "valid": self.valid,
            "stages": dict(self.stages),
            "issues": [asdict(issue) for issue in self.issues],
            "summary": self.summary,
        }


def _json_pointer(path: tuple[Any, ...]) -> str:
    result = "$"
    for part in path:
        if isinstance(part, int):
            result += f"[{part}]"
        else:
            result += f".{part}"
    return result


def _schema_issues(data: Any, schema: dict[str, Any]) -> list[ValidationIssue]:
    validator = jsonschema.Draft7Validator(schema)
    errors = sorted(
        validator.iter_errors(data),
        key=lambda error: ([str(part) for part in error.absolute_path], error.message),
    )
    return [
        ValidationIssue(
            stage="schema",
            message=error.message,
            json_path=_json_pointer(tuple(error.absolute_path)),
        )
        for error in errors
    ]


def _graph_documents(data: dict[str, Any]) -> list[tuple[str, dict[str, Any]]]:
    graphs = [("$.graph", data.get("graph") or {})]
    for index, sub in enumerate(data.get("subcomponents") or []):
        graphs.append((f"$.subcomponents[{index}].graph", sub.get("graph") or {}))
    return graphs


def _check_raw_document(
    data: dict[str, Any], report: ProjectValidationReport, registry: Registry
) -> bool:
    """Check relationships that JSON Schema cannot express and the model loader absorbs."""
    raw_tasks = data.get("tasks") or []
    task_ids = {
        task.get("id") for task in raw_tasks if isinstance(task, dict) and task.get("id") is not None
    }
    if not task_ids:
        task_ids = {"default"}

    structural_errors = 0
    task_id_occurrences: list[str] = []
    for index, task in enumerate(raw_tasks):
        task_id = task.get("id")
        if task_id is None:
            continue
        if task_id in task_id_occurrences:
            report.add_error(
                "structure",
                f"重复的 Task id: {task_id}",
                f"$.tasks[{index}].id",
            )
            structural_errors += 1
        task_id_occurrences.append(task_id)

    sub_ids: set[str] = set()
    for index, sub in enumerate(data.get("subcomponents") or []):
        sub_id = sub.get("id")
        if sub_id is None:
            continue
        if sub_id in sub_ids:
            report.add_error(
                "structure",
                f"重复的 subcomponent id: {sub_id}",
                f"$.subcomponents[{index}].id",
            )
            structural_errors += 1
        sub_ids.add(sub_id)

    component_errors = 0
    for graph_path, graph in _graph_documents(data):
        node_ids: set[str] = set()
        for index, node in enumerate(graph.get("nodes") or []):
            node_id = node.get("id")
            if node_id is None:
                continue
            if node_id in node_ids:
                report.add_error(
                    "structure",
                    f"重复的节点 id: {node_id}",
                    f"{graph_path}.nodes[{index}].id",
                )
                structural_errors += 1
            node_ids.add(node_id)

            task = node.get("task", "default")
            if task not in task_ids:
                report.add_error(
                    "structure",
                    f"节点 {node_id} 引用不存在的 Task: {task}",
                    f"{graph_path}.nodes[{index}].task",
                )
                structural_errors += 1

            component = node.get("component")
            if isinstance(component, str) and component.startswith("sub:"):
                sub_id = component[4:]
                if sub_id not in sub_ids:
                    report.add_error(
                        "component",
                        f"节点 {node_id} 引用不存在的子组件: {component}",
                        f"{graph_path}.nodes[{index}].component",
                    )
                    component_errors += 1
            elif isinstance(component, str) and registry.get(component) is None:
                report.add_error(
                    "component",
                    f"组件不存在: {component}（节点 {node_id}）",
                    f"{graph_path}.nodes[{index}].component",
                )
                component_errors += 1

        seen_connections: set[tuple[str, str]] = set()
        for index, connection in enumerate(graph.get("connections") or []):
            key = (connection.get("from"), connection.get("to"))
            if key in seen_connections:
                report.add_error(
                    "structure",
                    f"重复的连接: {key[0]} -> {key[1]}",
                    f"{graph_path}.connections[{index}]",
                )
                structural_errors += 1
            seen_connections.add(key)

    report._set_stage("structure", "failed" if structural_errors else "passed")
    report._set_stage("references", "failed" if component_errors else "passed")
    return structural_errors > 0, component_errors > 0


def _check_component_assets(
    component_id: str, node_id: str, info: Any, report: ProjectValidationReport
) -> None:
    """Verify files explicitly declared by a referenced source component."""
    if info.package_type != "source":
        return

    for key in ("sources", "headers"):
        for relative_path in info.manifest.get(key) or []:
            file_path = info.root_dir / relative_path
            if not file_path.is_file():
                report.add_error(
                    "component",
                    f"组件 {component_id}（节点 {node_id}）声明的 {key[:-1]} 不存在: "
                    f"{relative_path}",
                )




def _check_parameter(
    node_id: str,
    node_index: int,
    param: dict[str, Any],
    value: Any,
    report: ProjectValidationReport,
    graph_path: str,
) -> None:
    param_id = param.get("id")
    value_path = f"{graph_path}.nodes[{node_index}].params.{param_id}"
    param_type = param.get("type", "float")

    valid_type = {
        "float": isinstance(value, (int, float)) and not isinstance(value, bool),
        "int": isinstance(value, int) and not isinstance(value, bool),
        "bool": isinstance(value, bool),
        "string": isinstance(value, str),
    }.get(param_type, True)
    if not valid_type:
        report.add_error(
            "component",
            f"节点 {node_id} 参数 {param_id} 类型应为 {param_type}，实际为 "
            f"{type(value).__name__}",
            value_path,
        )
        return

    param_range = param.get("range")
    if (
        param_range
        and len(param_range) == 2
        and all(isinstance(bound, (int, float)) and not isinstance(bound, bool) for bound in param_range)
        and isinstance(value, (int, float))
        and not isinstance(value, bool)
    ):
        if not param_range[0] <= value <= param_range[1]:
            report.add_error(
            "component",
            f"节点 {node_id} 参数 {param_id} 值 {value} 超出范围 "
                f"[{param_range[0]}, {param_range[1]}]",
                value_path,
            )


def _check_loaded_project(
    project: Project,
    project_file: Path,
    registry: Registry,
    report: ProjectValidationReport,
    graph_path: str = "graph",
) -> bool:
    """Check project references and component-facing parameters before graph compile."""
    subs = {sub.id: sub for sub in project.subcomponents}
    graphs = [(graph_path, project.graph)]
    for index, sub in enumerate(project.subcomponents):
        graphs.append((f"$.subcomponents[{index}].graph", sub.graph))

    component_errors = 0
    for current_path, graph in graphs:
        for node_index, node in enumerate(graph.nodes.values()):
            if node.component.startswith("sub:"):
                sub_id = node.component[4:]
                component_errors += 1
                continue
                sub = subs[sub_id]
                input_params = {
                    public.id for public in sub.public_parameters if public.direction == "input"
                }
                output_params = {
                    public.id for public in sub.public_parameters if public.direction == "output"
                }
                for param_id, value in node.params.items():
                    if param_id in output_params:
                        report.add_error(
                            "component",
                            f"节点 {node.id} 不能给子组件输出参数赋值: {param_id}",
                            f"{current_path}.nodes[{node_index}].params.{param_id}",
                        )
                    elif param_id not in input_params:
                        report.add_error(
                            "component",
                            f"节点 {node.id} 的子组件 {node.component} 没有输入参数 {param_id}",
                            f"{current_path}.nodes[{node_index}].params.{param_id}",
                        )
                continue

            info = registry.get(node.component)
            if info is None:
                component_errors += 1
                continue

            if node.version and node.version != info.version:
                report.add_warning(
                    "component",
                    f"节点 {node.id} 声明组件版本 {node.version}，注册表实际为 {info.version}",
                    f"{current_path}.nodes[{node_index}].version",
                )

            _check_component_assets(node.component, node.id, info, report)
            parameters = {
                param["id"]: param for param in info.manifest.get("parameters") or []
            }
            for param_id, value in node.params.items():
                param = parameters.get(param_id)
                if param is None:
                    report.add_error(
                        "component",
                        f"组件 {node.component} 没有参数 {param_id}（节点 {node.id}）",
                        f"{current_path}.nodes[{node_index}].params.{param_id}",
                    )
                else:
                    _check_parameter(
                        node.id, node_index, param, value, report, current_path
                    )

            file_path = node.params.get("file_path")
            if (
                node.component.endswith("_in")
                and isinstance(file_path, str)
                and not file_path.startswith(("http://", "https://"))
            ):
                asset_path = (project_file.parent / file_path).resolve()
                if not asset_path.is_file():
                    report.add_error(
                        "asset",
                        f"节点 {node.id} 引用的输入文件不存在: {file_path}",
                        f"{current_path}.nodes[{node_index}].params.file_path",
                    )

    return component_errors > 0


def validate_project(
    project_file: Path,
    registry: Registry,
    *,
    target: str | None = None,
    compile_graph: bool = True,
    display_root: Path | None = None,
) -> ProjectValidationReport:
    """Validate one project and collect every independent finding."""
    resolved_file = project_file.resolve()
    if display_root:
        try:
            display_path = resolved_file.relative_to(Path(display_root).resolve())
        except ValueError:
            display_path = resolved_file
    else:
        display_path = resolved_file

    report = ProjectValidationReport(path=display_path)
    schema = schemas.load_project_schema()

    try:
        with open(resolved_file, "r", encoding="utf-8") as stream:
            data = yaml.safe_load(stream)
    except (OSError, UnicodeError, yaml.YAMLError) as exc:
        report.add_error("yaml", f"无法读取或解析 YAML: {exc}")
        report._set_stage("yaml", "failed")
        report._set_stage("schema", "skipped")
        report._set_stage("project_model", "skipped")
        report._set_stage("references", "skipped")
        report._set_stage("compile", "skipped")
        _finalize_summary(report)
        return report

    report._set_stage("yaml", "passed")
    issues = _schema_issues(data, schema)
    report.issues.extend(issues)
    report._set_stage("schema", "failed" if issues else "passed")
    if not isinstance(data, dict):
        report.add_error("yaml", "工程根节点必须是 mapping")
        report._set_stage("project_model", "skipped")
        report._set_stage("references", "skipped")
        report._set_stage("compile", "skipped")
        _finalize_summary(report)
        return report

    structure_failed, reference_failed = _check_raw_document(data, report, registry)
    if report.stages.get("schema") == "failed":
        report._set_stage("project_model", "skipped")
        report._set_stage("compile", "skipped")
        _finalize_summary(report)
        return report

    try:
        project = ProjectLoader().load(resolved_file)
    except Exception as exc:
        report.add_error("project_model", f"无法构造工程模型: {exc}")
        report._set_stage("project_model", "failed")
        report._set_stage("compile", "skipped")
        _finalize_summary(report)
        return report

    report._set_stage("project_model", "passed")
    loaded_reference_failed = _check_loaded_project(
        project, resolved_file, registry, report
    )
    report._set_stage(
        "references",
        "failed" if reference_failed or loaded_reference_failed else "passed",
    )

    if not compile_graph:
        report._set_stage("compile", "skipped")
        _finalize_summary(report)
        return report

    if structure_failed or report.stages.get("references") == "failed":
        report._set_stage("compile", "skipped")
        _finalize_summary(report)
        return report

    try:
        flat = flatten_project(project)
        plan = GraphCompiler(registry).compile(flat, target=target)
        if plan.ignored_nodes:
            report.add_warning(
                "compile",
                "debug_mode 忽略了孤立节点: " + ", ".join(plan.ignored_nodes),
            )
        report._set_stage("compile", "passed")
    except CompileError as exc:
        report.add_error("compile", str(exc))
        report._set_stage("compile", "failed")
    except Exception as exc:
        report.add_error("compile", f"编译器发生未分类错误: {type(exc).__name__}: {exc}")
        report._set_stage("compile", "failed")

    _finalize_summary(report)
    return report


def _finalize_summary(report: ProjectValidationReport) -> None:
    errors = [issue for issue in report.issues if issue.severity == "error"]
    warnings = [issue for issue in report.issues if issue.severity == "warning"]
    report.summary = {
        "errors": len(errors),
        "warnings": len(warnings),
        "target": None,
    }


def discover_project_files(paths: list[Path]) -> list[Path]:
    """Expand files or directories into project YAML files."""
    files: set[Path] = set()
    for path in paths:
        if path.is_file():
            files.add(path.resolve())
        elif path.is_dir():
            files.update(path.rglob("*.yaml"))
            files.update(path.rglob("*.yml"))
    return sorted(files)
