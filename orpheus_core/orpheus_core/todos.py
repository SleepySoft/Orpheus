"""工程节点 TODO 清单；不依赖编译，不遗漏未实例化的子图定义。"""

from __future__ import annotations

from typing import Any

from orpheus_core.placeholder import COMPONENT_ID
from orpheus_core.project import Project


def collect_todos(project: Project, *, include_done: bool = False) -> dict[str, Any]:
    items: list[dict[str, Any]] = []
    graphs = [("main", project.graph)]
    graphs.extend((f"sub:{sub.id}", sub.graph) for sub in project.subcomponents)
    for view, graph in graphs:
        for node in graph.nodes.values():
            records = list(node.todos)
            info = project.registry.get(node.component) if project.registry else None
            is_placeholder = node.component == COMPONENT_ID or bool(
                info and info.package_type == "placeholder"
            )
            # 即使误删 TODO 或把它标为完成，占位模块仍须出现在未完成清单。
            if is_placeholder and not any(todo["status"] != "done" for todo in records):
                records.append({
                    "id": "__placeholder__",
                    "title": "替换占位模块并验证算法；补充源码索引与下一步",
                    "status": "pending",
                    "synthetic": True,
                })
            if info and info.package_type == "placeholder":
                for blocker in info.manifest.get("blockers", []):
                    records.append({
                        "id": f"__blocker__{blocker['id']}",
                        "title": blocker["reason"],
                        "status": "blocked",
                        "source_ref": blocker.get("source_ref", ""),
                        "synthetic": True,
                    })
            for todo in records:
                if include_done or todo["status"] != "done":
                    items.append({
                        **todo,
                        "view": view,
                        "node": node.id,
                        "component": node.component,
                        "location": f"{view}/{node.id}#{todo['id']}",
                    })
    return {
        "scope": "工程主图及全部已加载子图定义；共享定义只列一次，含未实例化定义",
        "summary": {
            "total": len(items),
            "open": sum(item["status"] != "done" for item in items),
            "blocked": sum(item["status"] == "blocked" for item in items),
        },
        "items": items,
    }
