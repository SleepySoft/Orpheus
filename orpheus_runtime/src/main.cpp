#include "orpheus_runtime/plan.h"
#include "orpheus_runtime/runtime.h"
#include "orpheus_runtime/wav_io.h"

#include <iostream>
#include <string>

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <plan.json> <component_dir>" << std::endl;
}

static std::string find_input_wav(const orpheus::Plan& plan) {
    for (const auto& kv : plan.node_configs) {
        if (kv.second.component == "orpheus.builtin.wav_in") {
            auto it = kv.second.params.find("file_path");
            if (it != kv.second.params.end()) {
                return it->second;
            }
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

        // Determine total frames from input WAV
        std::string input_wav = find_input_wav(plan);
        uint32_t total_frames = 0;
        if (!input_wav.empty()) {
            std::vector<float> samples;
            orpheus::WavInfo info;
            if (orpheus::wav_read_f32(input_wav, samples, info)) {
                total_frames = info.total_frames;
                std::cout << "Input: " << input_wav << " " << info.total_frames
                          << " frames @ " << info.sample_rate << " Hz" << std::endl;
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
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
