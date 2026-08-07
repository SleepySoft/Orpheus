#include "orpheus_runtime/plan.h"
#include "orpheus_runtime/runtime.h"
#include "orpheus_runtime/wav_io.h"

#include <iostream>
#include <string>

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <plan.json> <component_dir>" << std::endl;
}

static std::string find_input_node(const orpheus::Plan& plan) {
    for (const auto& kv : plan.node_configs) {
        if (kv.second.component == "orpheus.builtin.wav_in" ||
            kv.second.component == "orpheus.builtin.mp3_in") {
            return kv.first;
        }
    }
    return "";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string plan_path = argv[1];
    std::string component_dir = argv[2];

    try {
        orpheus::Plan plan = orpheus::Plan::load_from_file(plan_path);

        orpheus::Runtime runtime;
        int rc = runtime.load_plan(plan, component_dir);
        if (rc != 0) {
            std::cerr << "Failed to load plan: " << rc << std::endl;
            return 1;
        }

        // Determine total frames from the file input node
        std::string input_node = find_input_node(plan);
        uint32_t total_frames = 0;
        if (!input_node.empty()) {
            const auto& cfg = plan.node_configs.at(input_node);
            auto it = cfg.params.find("file_path");
            std::string input_file = it != cfg.params.end() ? it->second : "";
            if (cfg.component == "orpheus.builtin.mp3_in") {
                OrpheusValue v;
                if (!input_file.empty() &&
                    runtime.get_parameter(input_node, "total_frames", &v) == ORPHEUS_OK &&
                    v.type == ORPHEUS_VALUE_INT) {
                    total_frames = (uint32_t)v.value.i32;
                    std::cout << "Input: " << input_file << " " << total_frames
                              << " frames (mp3, resampled to graph rate)" << std::endl;
                }
            } else if (!input_file.empty()) {
                std::vector<float> samples;
                orpheus::WavInfo info;
                if (orpheus::wav_read_f32(input_file, samples, info)) {
                    total_frames = info.total_frames;
                    std::cout << "Input: " << input_file << " " << info.total_frames
                              << " frames @ " << info.sample_rate << " Hz" << std::endl;
                }
            }
        }

        if (total_frames == 0) {
            total_frames = 48000 * 10; // 10 seconds default
        }

        uint32_t block_size = plan.block_size;
        uint32_t processed = 0;
        while (processed < total_frames) {
            uint32_t this_block = block_size;
            if (processed + this_block > total_frames) {
                this_block = total_frames - processed;
            }
            rc = runtime.process_block(this_block);
            if (rc != 0) {
                std::cerr << "Process block failed: " << rc << std::endl;
                return 1;
            }
            processed += this_block;
        }

        std::cout << "Processed " << processed << " frames" << std::endl;

        // Dump probe readback values: PROBE <node> <param> <value>
        for (const auto& node_id : plan.execution_order) {
            // v2：探针发现统一走注册表（PROBE 槽）
            auto slots = runtime.probe_slots(node_id);
            for (const orpheus::SlotEntry* e : slots) {
                OrpheusValue v;
                if (runtime.get_parameter(node_id, e->key, &v) != ORPHEUS_OK) continue;
                if (v.type == ORPHEUS_VALUE_FLOAT) {
                    std::cout << "PROBE " << node_id << " " << e->key << " " << v.value.f32 << std::endl;
                } else if (v.type == ORPHEUS_VALUE_INT) {
                    std::cout << "PROBE " << node_id << " " << e->key << " " << v.value.i32 << std::endl;
                } else if (v.type == ORPHEUS_VALUE_STRING) {
                    // 复合/结构化探针（波形/频谱 JSON）：整行
                    std::cout << "PROBE_JSON " << node_id << " " << e->key << " " << v.value.str << std::endl;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
