"""无需启动服务的组件声明只读校验入口。"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "orpheus_core"))
from orpheus_core.component_definition import cli_main

if __name__ == "__main__":
    raise SystemExit(cli_main())
