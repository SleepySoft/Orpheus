"""Teaching-package checks for project documents."""

from __future__ import annotations

from typing import Any

from orpheus_core.compiler import ExecutionPlan
from orpheus_core.project import Project


def _result(check: dict[str, Any], passed: bool, detail: str) -> dict[str, Any]:
    return {
        "id": check.get("id", check.get("type", "check")),
        "label": check.get("label", check.get("id", check.get("type", "检查"))),
        "passed": passed,
        "detail": detail,
    }


def evaluate_lesson(
    project: Project,
    flat_project: Project | None,
    plan: ExecutionPlan | None,
    compile_error: str | None = None,
) -> dict[str, Any]:
    lesson = project.extra.get("lesson")
    if not isinstance(lesson, dict):
        return {"title": "", "steps": [], "results": [], "passed": True, "total": 0}

    graph = flat_project.graph if flat_project is not None else project.graph
    results: list[dict[str, Any]] = []
    for check in lesson.get("checks", []) or []:
        if not isinstance(check, dict):
            results.append(_result({}, False, "检查规则必须是对象"))
            continue
        kind = check.get("type")
        if kind == "compile_valid":
            results.append(_result(
                check,
                compile_error is None and plan is not None,
                "编译通过" if compile_error is None else compile_error,
            ))
            continue

        if kind == "node_component":
            node_id = check.get("node")
            if not isinstance(node_id, str) or not node_id:
                results.append(_result(check, False, "node_component 缺少有效 node"))
                continue
            expected = check.get("component")
            node = graph.nodes.get(node_id)
            actual = node.component if node is not None else None
            results.append(_result(
                check, actual == expected,
                f"{node_id}: {actual or '节点不存在'}" + ("" if actual == expected else f"，期望 {expected}"),
            ))
            continue

        if kind == "parameter_equals":
            node_id = check.get("node")
            param = check.get("param")
            if not isinstance(node_id, str) or not node_id or not isinstance(param, str) or not param:
                results.append(_result(check, False, "parameter_equals 缺少有效 node/param"))
                continue
            expected = check.get("value")
            node = graph.nodes.get(node_id)
            actual = node.params.get(param) if node is not None else None
            try:
                tolerance = float(check.get("tolerance", 0.0) or 0.0)
            except (TypeError, ValueError):
                results.append(_result(check, False, "parameter_equals 的 tolerance 必须是数字"))
                continue
            if isinstance(actual, (int, float)) and isinstance(expected, (int, float)):
                passed = abs(float(actual) - float(expected)) <= tolerance
            else:
                passed = actual == expected
            results.append(_result(
                check, passed,
                f"{node_id}.{param}={actual!r}" + ("" if passed else f"，期望 {expected!r}"),
            ))
            continue

        if kind in ("connection_exists", "control_connection_exists"):
            source = check.get("from")
            target = check.get("to")
            if not isinstance(source, str) or not source or not isinstance(target, str) or not target:
                results.append(_result(check, False, f"{kind} 缺少有效 from/to"))
                continue
            connections = (
                graph.connections if kind == "connection_exists"
                else (flat_project.control_connections if flat_project is not None else project.control_connections)
            )
            passed = any(str(edge.from_ref) == source and str(edge.to_ref) == target for edge in connections)
            results.append(_result(check, passed, f"{source} -> {target}"))
            continue

        if kind == "task_bridge_count":
            try:
                minimum = int(check.get("minimum", 1))
            except (TypeError, ValueError):
                results.append(_result(check, False, "task_bridge_count 的 minimum 必须是整数"))
                continue
            actual = 0 if plan is None else sum(
                1 for buffer in plan.buffers.values() if buffer.get("task_bridge")
            )
            results.append(_result(
                check, actual >= minimum, f"异步桥 {actual}，要求至少 {minimum}",
            ))
            continue

        results.append(_result(check, False, f"未知检查类型：{kind}"))

    passed_count = sum(1 for item in results if item["passed"])
    return {
        "title": lesson.get("title", "教学任务"),
        "description": lesson.get("description", ""),
        "steps": lesson.get("steps", []) or [],
        "results": results,
        "passed": passed_count == len(results),
        "passed_count": passed_count,
        "total": len(results),
    }
