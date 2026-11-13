"""SPORT/TDM 边界迁入宿主音频绑定后的生成工程数值验证。"""

import subprocess

import pytest
import yaml

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, run_cmake_with_msvc_env
from test_sport_integration import _project_document

RUNNER = r'''
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "orpheus_graph.h"
#include "orpheus_sport.h"

#define FRAMES 32u
#define SLOTS 16u
#define CALLS 8u

static float dec_q31(int32_t v) { return (float)v * (1.0f / 2147483648.0f); }
static int32_t enc_q31(float v) {
    if (v >= 1.0f) return INT32_MAX;
    if (v <= -1.0f) return INT32_MIN;
    return (int32_t)(v * 2147483648.0f);
}
static int32_t raw_at(unsigned frame, unsigned slot) {
    return (int32_t)(((frame * SLOTS + slot) * 2654435761u) % 2000000000u) - 1000000000;
}

int main(void) {
    static int32_t rx0[FRAMES * SLOTS], rx1[FRAMES * SLOTS];
    static int32_t tx0[FRAMES * SLOTS], tx1[FRAMES * SLOTS];
    if (orpheus_graph_init(48000, FRAMES) != 0) return 2;
    const float gain = powf(10.0f, -6.0f / 20.0f);
    OrpheusSportIo_audio io;
    io.sport0b_rx = rx0;
    io.sport0a_rx = rx1;
    io.sport2a_tx = tx0;
    io.sport2b_tx = tx1;
    for (uint64_t call = 0; call < CALLS; ++call) {
        for (unsigned f = 0; f < FRAMES; ++f)
            for (unsigned s = 0; s < SLOTS; ++s) {
                rx0[f * SLOTS + s] = raw_at((unsigned)call * FRAMES + f, s);
                rx1[f * SLOTS + s] = raw_at((unsigned)call * FRAMES + f, s + SLOTS);
            }
        memset(tx0, 0xA5, sizeof(tx0));
        memset(tx1, 0xA5, sizeof(tx1));
        OrpheusExternalTriggerContext trigger = {
            .epoch = 1, .frame_index = call * FRAMES, .sequence = call, .frames = FRAMES, .flags = 0};
        if (orpheus_graph_process_task_audio_sport(&io, &trigger) != 0) return 3;
        for (unsigned f = 0; f < FRAMES; ++f)
            for (unsigned s = 0; s < SLOTS; ++s) {
                uint64_t n = call * FRAMES + f;
                int32_t want0 = enc_q31(dec_q31(raw_at((unsigned)n, s)) * gain);
                int32_t want1 = enc_q31(dec_q31(raw_at((unsigned)n, s + SLOTS)) * gain);
                if (tx0[f * SLOTS + s] != want0 || tx1[f * SLOTS + s] != want1) {
                    printf("mismatch call=%llu f=%u s=%u got=%d/%d want=%d/%d\n",
                        (unsigned long long)call, f, s, tx0[f * SLOTS + s], tx1[f * SLOTS + s],
                        want0, want1);
                    return 4;
                }
            }
    }
    const OrpheusTriggerStats* stats = orpheus_generated_trigger_stats_audio();
    if (!stats || stats->calls != CALLS || stats->sequence_errors || stats->frame_errors) {
        printf("bad stats calls=%llu seq=%llu frm=%llu\n",
            stats ? (unsigned long long)stats->calls : 0ull,
            stats ? (unsigned long long)stats->sequence_errors : 0ull,
            stats ? (unsigned long long)stats->frame_errors : 0ull);
        return 5;
    }
    orpheus_graph_teardown();
    printf("ok %llu\n", (unsigned long long)stats->calls);
    return 0;
}
'''


def _build_generated(tmp_path, plan, registry):
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src" / "sport_runner.c").write_text(RUNNER, encoding="utf-8")
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as file:
        file.write("\nadd_executable(sport_runner src/sport_runner.c)\n"
                   "target_link_libraries(sport_runner PRIVATE orpheus_graph)\n")
    build = tmp_path / "build"
    configure = ["cmake", "-S", str(generated), "-B", str(build), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release"]
    for line in (ROOT / "build" / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith(("CMAKE_C_COMPILER:", "CMAKE_CXX_COMPILER:")):
            configure.append(f"-D{line.split(':', 1)[0]}={line.split('=', 1)[1]}")
    for command in (configure, ["cmake", "--build", str(build)]):
        result = run_cmake_with_msvc_env(command, tmp_path, ROOT / "build")
        assert result.returncode == 0, result.stdout + result.stderr
    return build / "sport_runner.exe"


def test_sport_dma_roundtrip_bit_exact(tmp_path):
    document = _project_document()
    path = tmp_path / "sport.yaml"
    path.write_text(yaml.safe_dump(document, sort_keys=False), encoding="utf-8")
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    plan = GraphCompiler(registry).compile(ProjectLoader().load(path), target="adsp21593")
    runner = _build_generated(tmp_path, plan, registry)
    result = subprocess.run([str(runner)], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.strip() == "ok 8"
