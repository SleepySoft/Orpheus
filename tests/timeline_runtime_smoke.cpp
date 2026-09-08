#include "orpheus_runtime/runtime.h"

#include <cmath>
#include <string>

static orpheus::Plan make_plan(bool with_task, uint32_t period = 1) {
    orpheus::Plan plan;
    plan.sample_rate = 48000;
    plan.block_size = 128;
    plan.schedule_tick = 128;
    plan.task_id = "default";
    plan.nodes.push_back("probe");
    plan.execution_order.push_back("probe");

    orpheus::NodeConfig node;
    node.id = "probe";
    node.component = "orpheus.test.timeline_probe";
    node.task = with_task ? "timeline_task" : "default";
    node.frames = 128;
    node.block_size = 128;
    node.sample_rate = 48000 / period;
    node.period = period;
    plan.node_configs[node.id] = node;

    orpheus::ModuleConfig root;
    root.path = "";
    root.id = 0;
    root.leaves.push_back(std::make_pair(std::string("probe"), 0u));
    plan.modules.push_back(root);

    if (with_task) {
        orpheus::TaskConfig task;
        task.id = "timeline_task";
        task.name = "Timeline Task";
        task.sample_rate = 48000;
        task.block_size = 128;
        task.schedule_tick = 128;
        task.nodes.push_back("probe");
        task.execution_order.push_back("probe");
        task.periods["probe"] = period;
        plan.tasks.push_back(task);
    }
    return plan;
}

static int read_int(orpheus::Runtime& runtime, const char* key) {
    OrpheusValue value;
    if (runtime.get_parameter("probe", key, &value) != ORPHEUS_OK) return -9999;
    return value.value.i32;
}

static float read_float(orpheus::Runtime& runtime, const char* key) {
    OrpheusValue value;
    if (runtime.get_parameter("probe", key, &value) != ORPHEUS_OK) return -9999.0f;
    return value.value.f32;
}

static int verify(orpheus::Runtime& runtime, bool task_mode) {
    const auto process = [&]() {
        return task_mode
            ? runtime.process_task("timeline_task", 128)
            : runtime.process_block(128);
    };

    if (process() != ORPHEUS_OK) return 10;
    if (read_int(runtime, "frame_index") != 0) return 11;
    if (read_int(runtime, "epoch") != 1) return 12;
    if ((read_int(runtime, "flags") & ORPHEUS_TIMELINE_DISCONTINUITY) == 0) return 13;
    if (std::fabs(read_float(runtime, "timestamp")) > 1e-7f) return 14;

    if (process() != ORPHEUS_OK) return 20;
    if (read_int(runtime, "frame_index") != 128) return 21;
    if (read_int(runtime, "epoch") != 1) return 22;
    if (read_int(runtime, "flags") != ORPHEUS_TIMELINE_NONE) return 23;
    if (std::fabs(read_float(runtime, "timestamp") - 128.0f / 48000.0f) > 1e-7f) return 24;
    return 0;
}

static int verify_derived_rate(const char* component_dir) {
    orpheus::Runtime runtime;
    if (runtime.load_plan(make_plan(false, 2), component_dir) != ORPHEUS_OK) return 201;

    if (runtime.process_block(128) != ORPHEUS_OK) return 202;
    if (read_int(runtime, "call_count") != 0) return 203;
    if (runtime.process_block(128) != ORPHEUS_OK) return 204;
    if (read_int(runtime, "call_count") != 1) return 205;
    if (read_int(runtime, "frame_index") != 0) return 206;
    if ((read_int(runtime, "flags") & ORPHEUS_TIMELINE_DISCONTINUITY) == 0) return 207;
    if (std::fabs(read_float(runtime, "timestamp")) > 1e-7f) return 208;

    if (runtime.process_block(128) != ORPHEUS_OK) return 209;
    if (runtime.process_block(128) != ORPHEUS_OK) return 210;
    if (read_int(runtime, "call_count") != 2) return 211;
    if (read_int(runtime, "frame_index") != 128) return 212;
    if (read_int(runtime, "flags") != ORPHEUS_TIMELINE_NONE) return 213;
    if (std::fabs(read_float(runtime, "timestamp") - 128.0f / 24000.0f) > 1e-7f) return 214;
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;

    orpheus::Runtime global_runtime;
    if (global_runtime.load_plan(make_plan(false), argv[1]) != ORPHEUS_OK) return 3;
    int result = verify(global_runtime, false);
    if (result != 0) return result;

    orpheus::Runtime task_runtime;
    if (task_runtime.load_plan(make_plan(true), argv[1]) != ORPHEUS_OK) return 4;
    result = verify(task_runtime, true);
    if (result != 0) return result + 100;
    return verify_derived_rate(argv[1]);
}
