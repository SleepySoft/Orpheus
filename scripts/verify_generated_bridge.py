"""验证生成 PC 程序的 stdio、本地 Pipe 和 TCP Bridge Adapter。"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any

from orpheus_core.bridge import (
    BridgeSession,
    LengthPrefixCodec,
    LocalPipeTransport,
    ProcessTransport,
    TcpTransport,
)


def _read_stderr(process: subprocess.Popen[Any], lines: list[str]) -> None:
    if process.stderr is None:
        return
    for raw in iter(process.stderr.readline, b""):
        line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        lines.append(line)
        print(line, file=sys.stderr)


def _wait_ready(
    process: subprocess.Popen[Any], lines: list[str], timeout: float
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready = [line for line in lines if line.startswith("BRIDGE_READY ")]
        if ready:
            return json.loads(ready[-1][len("BRIDGE_READY ") :])
        if process.poll() is not None:
            raise RuntimeError(f"生成程序提前退出，退出码 {process.returncode}")
        time.sleep(0.05)
    raise TimeoutError("等待 BRIDGE_READY 超时")


def _start_detached(argv: list[str], timeout: float) -> tuple[
    subprocess.Popen[Any], list[str], dict[str, Any]
]:
    process = subprocess.Popen(
        argv,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    lines: list[str] = []
    thread = threading.Thread(target=_read_stderr, args=(process, lines), daemon=True)
    thread.start()
    try:
        ready = _wait_ready(process, lines, timeout)
    except Exception:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3.0)
        thread.join(timeout=1.0)
        raise
    return process, lines, ready


def _verify_session(
    transport: Any,
    manifest: dict[str, Any],
    label: str,
    node: str,
    key: str,
) -> None:
    session = BridgeSession(
        transport,
        LengthPrefixCodec(),
        manifest["id_map"],
        call_timeout=2.0,
        call_retries=5,
        probe_interval=0.1,
    )
    try:
        connect_result = session.connect(
            expected_id_map_hash=int(manifest["id_map_hash"], 16)
        )
        if not session.identity_verified:
            raise RuntimeError("id_map hash 不匹配")

        entry = next(
            item for item in manifest["id_map"]
            if item["node"] == node and item["key"] == key
        )
        old_value = session.read_id(entry["id"])
        new_value = float(old_value) + 7.5
        session.set_parameter(node, key, new_value)
        read_value = session.read_id(entry["id"])
        if abs(float(read_value) - float(old_value) - 7.5) > 0.0001:
            raise RuntimeError(f"参数读回不匹配: {read_value}")
        updated = session.poll_observations()
        stats = session.bridge_stats()

        session.stop_endpoint()
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            if not session.running:
                break
            time.sleep(0.05)
        print(
            f"[{label}] ok: identity={connect_result['identity']['id_map_hash']}, "
            f"parameter={old_value}->{read_value}, probes={updated}, stats={stats}"
        )
    finally:
        session.close()


def _verify_stdio(
    executable: Path, manifest: dict[str, Any], node: str, key: str,
) -> None:
    transport = ProcessTransport(
        [str(executable), "--bridge", "stdio"],
        cwd=executable.parent.parent,
    )
    try:
        _verify_session(transport, manifest, "stdio", node, key)
    finally:
        transport.process.wait(timeout=3.0)


def _verify_pipe(
    executable: Path, manifest: dict[str, Any], name: str,
    node: str, key: str,
) -> None:
    process, _lines, ready = _start_detached(
        [str(executable), "--bridge", "pipe", "--pipe-name", name],
        timeout=10.0,
    )
    endpoint = ready["endpoints"][0]
    if os.name == "nt":
        address = rf"\\.\pipe\{endpoint.split('://', 1)[1]}"
    else:
        address = endpoint.replace("unix://", "", 1)
    try:
        transport = LocalPipeTransport.connect(address)
        _verify_session(transport, manifest, "pipe", node, key)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3.0)


def _verify_tcp(
    executable: Path, manifest: dict[str, Any], node: str, key: str,
) -> None:
    process, _lines, ready = _start_detached(
        [str(executable), "--bridge", "tcp", "--port", "0"],
        timeout=10.0,
    )
    endpoint = ready["endpoints"][0]
    _, address = endpoint.split("://", 1)
    host, port_text = address.rsplit(":", 1)
    try:
        transport = TcpTransport.connect(host, int(port_text))
        _verify_session(transport, manifest, "tcp", node, key)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3.0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--pipe-name", default="verify-generated-bridge")
    parser.add_argument("--node", default="auto_gain")
    parser.add_argument("--key", default="smoothing_ms")
    args = parser.parse_args()

    executable = args.executable.resolve()
    manifest_path = (args.manifest or executable.parent.parent / "orpheus_app_manifest.json").resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    _verify_stdio(executable, manifest, args.node, args.key)
    _verify_pipe(executable, manifest, args.pipe_name, args.node, args.key)
    _verify_tcp(executable, manifest, args.node, args.key)
    print("全部 Bridge Adapter 验证通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
