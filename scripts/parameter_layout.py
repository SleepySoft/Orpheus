"""数据 layout 检查脚本：打印工程（含嵌套子组件）的树形数据点布局，并导出可读 JSON。

用法:
    python scripts/parameter_layout.py <project.yaml> [--out <export.json>]
"""

from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path

from orpheus_core.parameter_catalog import (
    apply_payload,
    build_catalog,
    export_payload,
    render_tree,
)
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry


def main() -> int:
    parser = argparse.ArgumentParser(description="打印/导出 Orpheus 工程数据点布局")
    parser.add_argument("project", type=Path, help="project.yaml 路径")
    parser.add_argument("--out", type=Path, default=None, help="导出 JSON 路径（默认不写文件）")
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    registry = Registry()
    registry.add_search_path(root / "components")
    registry.scan()

    project = ProjectLoader().load(args.project)
    print(render_tree(project, registry))

    payload = export_payload(project, registry)
    text = json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=False)
    print("\n# 导出 JSON（{} 节点）预览（前 1200 字符）：".format(len(payload["nodes"])))
    print(text[:1200])

    # 回写往返校验：应用后每个导出值都应出现在节点参数里
    cloned = copy.deepcopy(project)
    applied = apply_payload(cloned, payload, registry)
    ok = True
    cloned_entries = {e.flat_id: e for e in build_catalog(cloned, registry)}
    for item in payload["nodes"]:
        entry = cloned_entries.get(item["node"])
        for key, value in item["values"].items():
            if entry is None or entry.node_params.get(key) != value:
                ok = False
                print(f"MISMATCH {item['node']}.{key}: {value!r}")
    print(f"\n# 回写校验：应用 {applied}/{len(payload['nodes'])} 节点，{'通过' if ok else '失败'}")
    if args.out:
        args.out.write_text(text, encoding="utf-8")
        print(f"# 已写入 {args.out}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
