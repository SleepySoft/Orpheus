"""E2E smoke against a running server: subcomponent compile+run over HTTP."""

import json
import shutil
import sys
import urllib.request
from pathlib import Path

PORT = sys.argv[1] if len(sys.argv) > 1 else "8000"
BASE = f"http://127.0.0.1:{PORT}/api"
ROOT = Path(__file__).resolve().parents[1]
NAME = "sub_demo"


def req(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(
        BASE + path, data=data, method=method,
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(r) as resp:
        return resp.status, json.loads(resp.read() or b"{}")


# cleanup from previous runs
try:
    req("DELETE", f"/projects/{NAME}")
except Exception:
    pass

status, r = req("POST", "/projects", {"name": NAME})
print("create:", status, r["name"])

doc = {
    "version": "0.1.0",
    "metadata": {"name": NAME},
    "sample_rate": 48000,
    "block_size": 128,
    "subcomponents": [
        {
            "id": "chain",
            "name": "链路子组件",
            "ports": [
                {"id": "in1", "direction": "input", "maps_to": "gain:in"},
                {"id": "out1", "direction": "output", "maps_to": "biquad:out"},
            ],
            "graph": {
                "nodes": [
                    {"id": "gain", "component": "orpheus.builtin.gain",
                     "params": {"gain_db": -12.0, "channels": 2}, "position": {"x": 0, "y": 0}},
                    {"id": "biquad", "component": "orpheus.builtin.biquad",
                     "params": {"type": "lowpass", "fc": 1500.0, "q": 0.707,
                                "gain_db": 0.0, "channels": 2},
                     "position": {"x": 200, "y": 0}},
                ],
                "connections": [{"from": "gain:out", "to": "biquad:in"}],
            },
        }
    ],
    "graph": {
        "nodes": [
            {"id": "wav_in", "component": "orpheus.builtin.wav_in",
             "params": {"file_path": "test_input.wav", "channels": 2},
             "position": {"x": 0, "y": 0}},
            {"id": "chain_1", "component": "sub:chain", "params": {},
             "position": {"x": 200, "y": 0}},
            {"id": "wav_out", "component": "orpheus.builtin.wav_out",
             "params": {"file_path": "outputs/test_output.wav", "channels": 2,
                        "sample_rate": 48000},
             "position": {"x": 400, "y": 0}},
        ],
        "connections": [
            {"from": "wav_in:out", "to": "chain_1:in1"},
            {"from": "chain_1:out1", "to": "wav_out:in"},
        ],
    },
}

status, r = req("PUT", f"/projects/{NAME}", doc)
print("save:", status, r["status"])

status, r = req("GET", f"/projects/{NAME}")
print("readback subs:", status, r["subcomponents"][0]["id"],
      "ports:", [p["id"] for p in r["subcomponents"][0]["ports"]])

status, r = req("POST", f"/projects/{NAME}/compile")
print("compile:", status, r["status"], "nodes:", r["nodes"], "order:", r["execution_order"])

shutil.copy2(ROOT / "examples" / "test_input.wav", ROOT / "workspace" / NAME / "test_input.wav")
status, r = req("POST", f"/projects/{NAME}/run")
print("run:", status, r["status"], "outputs:", r["outputs"])

status, r = req("DELETE", f"/projects/{NAME}")
print("cleanup:", status, r["status"])
print("E2E OK")
sys.exit(0)
