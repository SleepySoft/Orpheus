"""画布布局旁文件；布局不参与组件结构与执行语义。"""
from __future__ import annotations

import copy
import math
from pathlib import Path
from typing import Any

import yaml

from orpheus_core import schemas

Document = dict[str, Any]


def reference_path(document: Document, owner: Path) -> Path | None:
    reference = document.get("layout")
    if reference is None:
        return None
    if not isinstance(reference, dict):
        raise ValueError("布局必须为对象或file引用。")
    if "file" not in reference:
        return None
    if set(reference) != {"file"} or not isinstance(reference["file"], str) or not reference["file"]:
        raise ValueError("布局引用必须只包含非空的file路径。")
    relative = Path(reference["file"])
    if relative.is_absolute() or relative.drive:
        raise ValueError("布局文件必须使用相对路径。")
    path = (owner.parent / relative).resolve()
    if not path.is_relative_to(owner.parent.resolve()) or path == owner.resolve():
        raise ValueError("布局文件不能覆盖结构文件或越出所属目录。")
    return path


def validate_layout(layout: Document) -> None:
    schemas.validate(layout, schemas.load_layout_schema())
    for view in layout.get("views", {}).values():
        for positions in (view.get("nodes", {}), view.get("bridges", {})):
            for position in positions.values():
                if any(not math.isfinite(value) for value in position.values()):
                    raise ValueError("布局坐标必须为有限数值。")


def apply_view(graph: Document, view: Document, *, override: bool = True) -> None:
    positions = view.get("nodes", {})
    for node in graph.get("nodes", []):
        if node["id"] in positions and (override or "position" not in node):
            node["position"] = copy.deepcopy(positions[node["id"]])


def apply_layout(document: Document, layout: Document, *, override: bool = True) -> Document:
    result = copy.deepcopy(document)
    views = layout.get("views", {})
    main = views.get("main", {})
    apply_view(result.get("graph", {}), main, override=override)
    for bridge in result.get("bridges", []):
        if bridge["id"] in main.get("bridges", {}) and (override or "position" not in bridge):
            bridge["position"] = copy.deepcopy(main["bridges"][bridge["id"]])
    for sub in result.get("subcomponents", []):
        apply_view(sub.get("graph", {}), views.get("sub:" + sub["id"], {}), override=override)
    return result


def load_document_layout(document: Document, owner: Path) -> tuple[Document, Document, Path | None]:
    path = reference_path(document, owner)
    if path is not None:
        with path.open(encoding="utf-8") as stream:
            layout = yaml.safe_load(stream)
    elif "layout" in document:
        layout = document["layout"]
    else:
        return document, {"version": 1, "views": {}}, None
    validate_layout(layout)
    return apply_layout(document, layout), layout, path


def split_document_layout(document: Document, *, extra_views: Document | None = None) -> tuple[Document, Document]:
    layout = document.get("layout", {})
    if layout and "file" not in layout:
        validate_layout(layout)
        document = apply_layout(document, layout, override=False)
    structure = copy.deepcopy(document)
    structure.pop("layout", None)
    views: Document = {}

    def extract(graph: Document) -> Document:
        return {"nodes": {node["id"]: node.pop("position", {})
                          for node in graph.get("nodes", [])}}

    main = extract(structure.get("graph", {}))
    if structure.get("bridges"):
        main["bridges"] = {bridge["id"]: bridge.pop("position", {})
                           for bridge in structure["bridges"]}
    views["main"] = main
    for sub in structure.get("subcomponents", []):
        views["sub:" + sub["id"]] = extract(sub.get("graph", {}))
    views.update(copy.deepcopy(extra_views or {}))
    result = {"version": 1, "views": views}
    validate_layout(result)
    return structure, result


def write_yaml_if_changed(path: Path, document: Document) -> None:
    if path.is_file():
        with path.open(encoding="utf-8") as stream:
            if yaml.safe_load(stream) == document:
                return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        yaml.safe_dump(document, stream, sort_keys=False, allow_unicode=True)


def save_document(
    document: Document, owner: Path, *, separate_layout: bool = True,
    extra_views: Document | None = None,
) -> None:
    if not separate_layout:
        if "layout" in document:
            _, layout, _ = load_document_layout(document, owner)
            inline = apply_layout(document, layout, override=False)
        else:
            inline = copy.deepcopy(document)
        inline.pop("layout", None)
        if extra_views:
            inline["layout"] = {"version": 1, "views": copy.deepcopy(extra_views)}
        write_yaml_if_changed(owner, inline)
        return
    structure, layout = split_document_layout(document, extra_views=extra_views)
    path = reference_path(document, owner)
    if path is None:
        default_name = owner.stem + ".layout.yaml"
        reference_path({"layout": {"file": default_name}}, owner)
        path = owner.with_name(default_name).resolve()
    if path.exists():
        with path.open(encoding="utf-8") as stream:
            validate_layout(yaml.safe_load(stream))
    structure["layout"] = {"file": path.relative_to(owner.parent.resolve()).as_posix()}
    write_yaml_if_changed(path, layout)
    write_yaml_if_changed(owner, structure)
