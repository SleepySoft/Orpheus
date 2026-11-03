"""复用布局采集驱动，逐位验证decode→encode对完整HC的恒等性。"""
from pathlib import Path
import shutil
import subprocess

import pytest

from orpheus_core.project import Connection, Node, PortRef
import test_halfcomplex_decode as decode_test

ROOT = Path(__file__).resolve().parents[2]


def verify_halfcomplex_encode(tmp_path, specifications, batches):
    cases = {}
    for name, params in specifications.items():
        assert params["bins"] == params["fft_size"]//2+1
        for suffix in ("before", "after", "typed"):
            cases[name+"_"+suffix] = {**params, "first_bin": 0, "typed": suffix=="typed"}
    build_original = decode_test.build_project

    def build_with_encoders(configs, count):
        project = build_original(configs, count)
        for name, params in specifications.items():
            for suffix in ("after", "typed"):
                after = name+"_"+suffix
                encoder = after+"_encode"
                project.graph.nodes[encoder] = Node(id=encoder, component="orpheus.builtin.halfcomplex_encode", params=params)
                project.graph.connections = [c for c in project.graph.connections
                                             if str(c.to_ref) not in (after+"_capture:packed", after+"_capture:spectrum")]
                extra = after+"_other_capture"
                project.graph.nodes[extra] = Node(id=extra, component="orpheus.test.halfcomplex_capture", params=params)
                primary = (":spectrum", ":spectrum") if suffix=="typed" else (":out", ":packed")
                secondary = (":out", ":packed") if suffix=="typed" else (":spectrum", ":spectrum")
                for source, target in ((after+":out", encoder+":in"),
                                       (encoder+primary[0], after+"_capture"+primary[1]),
                                       (encoder+secondary[0], extra+secondary[1]), (after+":out", extra+":values")):
                    project.graph.connections.append(Connection(PortRef.parse(source), PortRef.parse(target)))
        return project

    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target",
                             "orpheus_builtin_halfcomplex_encode"], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    libraries = tmp_path / "generated/build/components"
    libraries.mkdir(parents=True)
    for library in (ROOT / "build/components").glob("*orpheus_builtin_halfcomplex_encode.*"):
        if library.suffix in (".dll", ".so", ".dylib"):
            shutil.copy2(library, libraries / library.name)
    with pytest.MonkeyPatch.context() as patch:
        patch.setattr(decode_test, "build_project", build_with_encoders)
        decode_test.verify_halfcomplex_decode(tmp_path, cases, batches)
    data = (tmp_path / "halfcomplex.bin").read_bytes()
    cursor = 0
    for block in range(3):
        for name, params in specifications.items():
            size = (params["fft_size"]+params["bins"]*2)*batches*params["channels"]*4
            assert data[cursor:cursor+size] == data[cursor+size:cursor+size*2], (block, name)
            assert data[cursor:cursor+size] == data[cursor+size*2:cursor+size*3], (block, name, "typed")
            cursor += size*3
    assert cursor == len(data)


@pytest.mark.parametrize("batches,channels", [(1, 6), (3, 8)])
def test_halfcomplex_encode_four_entries(tmp_path, batches, channels):
    verify_halfcomplex_encode(tmp_path, {"encode": {
        "channels": channels, "fft_size": 256, "batches": batches, "bins": 129}}, batches)
