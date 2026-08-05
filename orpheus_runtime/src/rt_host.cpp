#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "orpheus_runtime/plan.h"
#include "orpheus_runtime/runtime.h"

#include <cstring>
#include <iostream>
#include <string>

struct HostContext {
    orpheus::Runtime* runtime;
    OrpheusBuffer* device_in_buf;
    OrpheusBuffer* device_out_buf;
    uint32_t channels;
};

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    HostContext* host = (HostContext*)pDevice->pUserData;
    if (!host || !host->runtime) return;

    float* out = (float*)pOutput;
    const float* in = (const float*)pInput;
    uint32_t ch = host->channels;

    // 1. Copy device input to device_in buffer
    if (host->device_in_buf && in) {
        float* buf = (float*)host->device_in_buf->data;
        for (ma_uint32 i = 0; i < frameCount * ch; ++i) {
            buf[i] = in[i];
        }
        host->device_in_buf->frame_count = frameCount;
    }

    // 2. Process graph
    host->runtime->process_block(frameCount);

    // 3. Copy device_out buffer to device output
    if (host->device_out_buf && out) {
        const float* buf = (const float*)host->device_out_buf->data;
        for (ma_uint32 i = 0; i < frameCount * ch; ++i) {
            out[i] = buf[i];
        }
    } else if (out) {
        std::memset(out, 0, frameCount * ch * sizeof(float));
    }
}

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <plan.json> <component_dir> [sample_rate] [block_size]" << std::endl;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string plan_path = argv[1];
    std::string component_dir = argv[2];
    uint32_t sample_rate = (argc > 3) ? (uint32_t)std::atoi(argv[3]) : 48000;
    uint32_t block_size = (argc > 4) ? (uint32_t)std::atoi(argv[4]) : 128;

    try {
        orpheus::Plan plan = orpheus::Plan::load_from_file(plan_path);
        plan.sample_rate = sample_rate;
        plan.block_size = block_size;

        orpheus::Runtime runtime;
        int rc = runtime.load_plan(plan, component_dir);
        if (rc != 0) {
            std::cerr << "Failed to load plan: " << rc << std::endl;
            return 1;
        }

        // Find device_in and device_out nodes
        std::string device_in_id;
        std::string device_out_id;
        for (const auto& kv : plan.node_configs) {
            if (kv.second.component == "orpheus.builtin.device_in") {
                device_in_id = kv.first;
            } else if (kv.second.component == "orpheus.builtin.device_out") {
                device_out_id = kv.first;
            }
        }

        if (device_in_id.empty() || device_out_id.empty()) {
            std::cerr << "Plan must contain device_in and device_out nodes" << std::endl;
            return 1;
        }

        HostContext host;
        host.runtime = &runtime;
        host.device_in_buf = runtime.get_output_buffer(device_in_id, "out");
        host.device_out_buf = runtime.get_input_buffer(device_out_id, "in");
        host.channels = 2;

        auto it = plan.node_configs.find(device_in_id);
        if (it != plan.node_configs.end()) {
            auto pit = it->second.params.find("channels");
            if (pit != it->second.params.end()) {
                host.channels = (uint32_t)std::atoi(pit->second.c_str());
            }
        }

        ma_result result;
        ma_device_config deviceConfig;
        ma_device device;

        deviceConfig = ma_device_config_init(ma_device_type_duplex);
        deviceConfig.capture.format = ma_format_f32;
        deviceConfig.capture.channels = host.channels;
        deviceConfig.playback.format = ma_format_f32;
        deviceConfig.playback.channels = host.channels;
        deviceConfig.sampleRate = sample_rate;
        deviceConfig.dataCallback = data_callback;
        deviceConfig.pUserData = &host;

        result = ma_device_init(NULL, &deviceConfig, &device);
        if (result != MA_SUCCESS) {
            std::cerr << "Failed to initialize audio device" << std::endl;
            return 1;
        }

        result = ma_device_start(&device);
        if (result != MA_SUCCESS) {
            std::cerr << "Failed to start audio device" << std::endl;
            ma_device_uninit(&device);
            return 1;
        }

        std::cout << "Real-time audio running. Press Enter to stop..." << std::endl;
        std::cin.get();

        ma_device_uninit(&device);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
