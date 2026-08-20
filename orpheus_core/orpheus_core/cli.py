"""Command-line interface for Orpheus Core."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import click

from orpheus_core.builder import BuildError, ComponentBuilder
from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry
from orpheus_core.subgraph import flatten_project


@click.group()
@click.option(
    "--project-root",
    type=click.Path(exists=True, file_okay=False, path_type=Path),
    default=Path.cwd(),
)
@click.pass_context
def cli(ctx: click.Context, project_root: Path) -> None:
    ctx.ensure_object(dict)
    ctx.obj["project_root"] = project_root


@cli.command()
@click.pass_context
def scan(ctx: click.Context) -> None:
    """Scan and list available components."""
    root = ctx.obj["project_root"]
    registry = Registry()
    registry.add_search_path(root / "components")
    registry.scan()
    for info in registry.list_components():
        click.echo(f"{info.id}\t{info.version}\t{info.package_type}\t{info.root_dir}")


@cli.command()
@click.argument("project_file", type=click.Path(exists=True, path_type=Path))
@click.option("--target", "target", default=None,
              help="目标平台覆盖（auto/win/dsp）；缺省读工程 target 字段")
@click.pass_context
def compile(ctx: click.Context, project_file: Path, target: str | None) -> None:
    """Compile a project YAML into an execution plan JSON."""
    root = ctx.obj["project_root"]
    registry = Registry()
    registry.add_search_path(root / "components")
    registry.scan()

    loader = ProjectLoader()
    project = loader.load(project_file)

    compiler = GraphCompiler(registry)
    try:
        plan = compiler.compile(flatten_project(project), target=target)
    except CompileError as exc:
        click.echo(f"compile error: {exc}", err=True)
        sys.exit(1)

    output = project_file.with_suffix(".plan.json")
    with open(output, "w", encoding="utf-8") as f:
        json.dump(plan.__dict__, f, indent=2, ensure_ascii=False)
    click.echo(f"execution plan written to {output}")


@cli.command()
@click.argument("component_ids", nargs=-1)
@click.option("--build-dir", type=click.Path(path_type=Path), default=None)
@click.pass_context
def build(ctx: click.Context, component_ids: tuple[str, ...], build_dir: Path | None) -> None:
    """Build one or more components."""
    root = ctx.obj["project_root"]
    registry = Registry()
    registry.add_search_path(root / "components")
    registry.scan()

    if build_dir is None:
        build_dir = root / "build"

    builder = ComponentBuilder(root, build_dir, registry)
    try:
        builder.configure()
    except BuildError as exc:
        click.echo(f"configure error: {exc}", err=True)
        sys.exit(1)

    full_build = not component_ids
    if full_build:
        component_ids = tuple(info.id for info in registry.list_components())

    for cid in component_ids:
        info = registry.get(cid)
        if info and info.manifest.get("execution", {}).get("none"):
            continue  # 声明式平台节点（如 platform_hook）：无运行时代码，不构建
        try:
            lib_path = builder.build_component(cid)
            click.echo(f"{cid} -> {lib_path}")
        except BuildError as exc:
            click.echo(f"build error for {cid}: {exc}", err=True)
            sys.exit(1)

    if full_build:
        # 完整构建：组件 + runtime/宿主（README 承诺 cli build = 全部组件 + runtime）。
        # 曾因只建组件导致 runtime 停留在旧 ABI，新组件读 config->state_block 越界（balance 异常）。
        for target in ("orpheus_runtime", "orpheus_rt_host"):
            result = builder._run_cmake(["cmake", "--build", str(build_dir), "--target", target])
            if result.returncode != 0:
                click.echo(f"build error for {target}:\n{result.stderr}", err=True)
                sys.exit(1)
            click.echo(f"{target} -> {build_dir / (target + '.exe')}")


@cli.command()
@click.argument("project_file", type=click.Path(exists=True, path_type=Path))
@click.argument("output_dir", type=click.Path(path_type=Path))
@click.option("--target", "target", default=None,
              help="目标平台覆盖（auto/win/dsp）；缺省读工程 target 字段")
@click.pass_context
def generate(ctx: click.Context, project_file: Path, output_dir: Path,
             target: str | None) -> None:
    """Generate a standalone C project from a project YAML."""
    root = ctx.obj["project_root"]
    registry = Registry()
    registry.add_search_path(root / "components")
    registry.scan()

    loader = ProjectLoader()
    project = loader.load(project_file)

    compiler = GraphCompiler(registry)
    try:
        plan = compiler.compile(flatten_project(project), target=target)
    except CompileError as exc:
        click.echo(f"compile error: {exc}", err=True)
        sys.exit(1)

    generator = CodeGenerator(registry, root)
    generator.generate(plan, output_dir)
    click.echo(f"generated project written to {output_dir}")


@cli.command()
@click.argument("name")
@click.pass_context
def new(ctx: click.Context, name: str) -> None:
    """Create a minimal project YAML."""
    root = ctx.obj["project_root"]
    path = root / "examples" / f"{name}.yaml"
    path.parent.mkdir(parents=True, exist_ok=True)
    loader = ProjectLoader()
    from orpheus_core.project import Graph, Project, Task

    project = Project(
        metadata={"name": name, "description": "Generated project"},
        sample_rate=48000,
        block_size=128,
    )
    project.tasks["default"] = Task(id="default", sample_rate=48000, block_size=128)
    project.graph = Graph()
    loader.save(project, path)
    click.echo(f"created {path}")


@cli.command("new-component")
@click.argument("name")
@click.option("--category", default="自定义", help="组件分类")
@click.pass_context
def new_component(ctx: click.Context, name: str, category: str) -> None:
    """Scaffold a custom component: ABI 骨架 + user 文件（隔离，重生成不覆盖）。"""
    from orpheus_core.scaffold import scaffold_custom_component

    root = ctx.obj["project_root"]
    try:
        path = scaffold_custom_component(root, name, category)
    except (FileExistsError, ValueError) as exc:
        click.echo(str(exc), err=True)
        sys.exit(1)
    click.echo(f"created custom component at {path}")
    click.echo(f"编辑 user/{name}_user.c 实现算法与 CUSTOM 消息；然后 python -m orpheus_core.cli build")


@cli.command()
@click.option("--host", default="127.0.0.1")
@click.option("--port", type=int, default=8000)
@click.option("--open", "open_browser", is_flag=True, help="Open the UI in a browser.")
@click.pass_context
def serve(ctx: click.Context, host: str, port: int, open_browser: bool) -> None:
    """Start the Orpheus server: API + hosted UI (if ui/build exists)."""
    import uvicorn

    from orpheus_core.server.app import create_app

    root = ctx.obj["project_root"]
    app = create_app(root)
    url = f"http://{host}:{port}"
    if (root / "ui" / "build" / "index.html").exists():
        click.echo(f"Orpheus UI + API available at {url}")
    else:
        click.echo(f"Orpheus API listening on {url}/api (UI build not found; run `cd ui && npm run build`)")
    if open_browser:
        import webbrowser

        webbrowser.open(url)
    uvicorn.run(app, host=host, port=port)


def main() -> None:
    cli()


if __name__ == "__main__":
    main()
