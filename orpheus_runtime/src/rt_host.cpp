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
    ma_pcm_rb* rb;  // loopback mode: ring buffer fed by the loopback capture device
};

// ---------------------------------------------------------------- helpers

static std::string get_param(const orpheus::Plan& plan, const std::string& node_id,
                             const std::string& param, const std::string& fallback = "") {
    auto it = plan.node_configs.find(node_id);
    if (it == plan.node_configs.end()) return fallback;
    auto pit = it->second.params.find(param);
    return pit != it->second.params.end() ? pit->second : fallback;
}

static int list_devices() {
    ma_context ctx;
    if (ma_context_init(NULL, 0, NULL, &ctx) != MA_SUCCESS) {
        std::cerr << "Failed to init audio context" << std::endl;
        return 1;
    }
    ma_device_info* pPlayback = nullptr;
    ma_device_info* pCapture = nullptr;
    ma_uint32 playbackCount = 0, captureCount = 0;
    ma_result r = ma_context_get_devices(&ctx, &pPlayback, &playbackCount, &pCapture, &captureCount);
    if (r != MA_SUCCESS) {
        std::cerr << "Failed to enumerate devices" << std::endl;
        ma_context_uninit(&ctx);
        return 1;
    }
    std::cout << "{\"playback\":[";
    for (ma_uint32 i = 0; i < playbackCount; ++i) {
        std::cout << (i ? "," : "") << "{\"name\":\"" << pPlayback[i].name
                  << "\",\"default\":" << (pPlayback[i].isDefault ? "true" : "false") << "}";
    }
    std::cout << "],\"capture\":[";
    for (ma_uint32 i = 0; i < captureCount; ++i) {
        std::cout << (i ? "," : "") << "{\"name\":\"" << pCapture[i].name
                  << "\",\"default\":" << (pCapture[i].isDefault ? "true" : "false") << "}";
    }
    std::cout << "]}" << std::endl;
    ma_context_uninit(&ctx);
    return 0;
}

// Find a device id by case-insensitive substring of its name. Returns false if no match.
static bool find_device_id(ma_device_type type, const std::string& match, ma_device_id* outId) {
    if (match.empty()) return false;
    ma_context ctx;
    if (ma_context_init(NULL, 0, NULL, &ctx) != MA_SUCCESS) return false;
    ma_device_info* pPlayback = nullptr;
    ma_device_info* pCapture = nullptr;
    ma_uint32 playbackCount = 0, captureCount = 0;
    bool found = false;
    if (ma_context_get_devices(&ctx, &pPlayback, &playbackCount, &pCapture, &captureCount) == MA_SUCCESS) {
        ma_device_info* infos = (type == ma_device_type_playback) ? pPlayback : pCapture;
        ma_uint32 count = (type == ma_device_type_playback) ? playbackCount : captureCount;
        std::string needle = match;
        for (auto& c : needle) c = (char)tolower((unsigned char)c);
        for (ma_uint32 i = 0; i < count && !found; ++i) {
            std::string name = infos[i].name;
            for (auto& c : name) c = (char)tolower((unsigned char)c);
            if (name.find(needle) != std::string::npos) {
                *outId = infos[i].id;
                found = true;
            }
        }
    }
    ma_context_uninit(&ctx);
    return found;
}

// ---------------------------------------------------------------- callbacks

// duplex (microphone) mode: capture+playback in one device
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    HostContext* host = (HostContext*)pDevice->pUserData;
    if (!host || !host->runtime) return;

    float* out = (float*)pOutput;
    const float* in = (const float*)pInput;
    uint32_t ch = host->channels;

    if (host->device_in_buf && in) {
        std::memcpy(host->device_in_buf->data, in, frameCount * ch * sizeof(float));
        host->device_in_buf->frame_count = frameCount;
    }

    host->runtime->process_block(frameCount);

    if (host->device_out_buf && out) {
        std::memcpy(out, host->device_out_buf->data, frameCount * ch * sizeof(float));
    } else if (out) {
        std::memset(out, 0, frameCount * ch * sizeof(float));
    }
}

// loopback mode: capture side pushes system mix into the ring buffer
void loopback_capture_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pOutput;
    HostContext* host = (HostContext*)pDevice->pUserData;
    if (!host || !host->rb || !pInput) return;
    ma_uint32 writable = frameCount;
    void* w = nullptr;
    if (ma_pcm_rb_acquire_write(host->rb, &writable, &w) == MA_SUCCESS && writable > 0) {
        std::memcpy(w, pInput, writable * host->channels * sizeof(float));
        ma_pcm_rb_commit_write(host->rb, writable);
    }
}

// loopback mode: playback side is the master clock; pull input from the ring buffer
void loopback_playback_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    HostContext* host = (HostContext*)pDevice->pUserData;
    if (!host || !host->runtime) return;
    float* out = (float*)pOutput;
    uint32_t ch = host->channels;

    if (host->device_in_buf) {
        float* buf = (float*)host->device_in_buf->data;
        ma_uint32 readable = frameCount;
        void* r = nullptr;
        ma_uint32 got = 0;
        if (host->rb && ma_pcm_rb_acquire_read(host->rb, &readable, &r) == MA_SUCCESS && readable > 0) {
            std::memcpy(buf, r, readable * ch * sizeof(float));
            ma_pcm_rb_commit_read(host->rb, readable);
            got = readable;
        }
        if (got < frameCount) {
            std::memset(buf + got * ch, 0, (frameCount - got) * ch * sizeof(float));
        }
        host->device_in_buf->frame_count = frameCount;
    }

    host->runtime->process_block(frameCount);

    if (host->device_out_buf && out) {
        std::memcpy(out, host->device_out_buf->data, frameCount * ch * sizeof(float));
    } else if (out) {
        std::memset(out, 0, frameCount * ch * sizeof(float));
    }
}

// ---------------------------------------------------------------- main

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <plan.json> <component_dir> [sample_rate] [block_size]\n"
              << "       " << prog << " --list-devices" << std::endl;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    if (std::string(argv[1]) == "--list-devices") {
        return list_devices();
    }
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string plan_path = argv[1];
    std::string component_dir = argv[2];
    uint32_t sample_rate = (argc > 3) ? (uint32_t)std::atoi(argv[3]) : 48000;
    uint32_t block_size = (argc > 4) ? (uint32_t)std::atoi(argv[4]) : 128;

    ma_pcm_rb rb;
    bool rb_inited = false;
    ma_device cap_device, play_device;
    bool cap_inited = false, play_inited = false;

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

        std::string device_in_id, device_out_id;
        for (const auto& kv : plan.node_configs) {
            if (kv.second.component == "orpheus.builtin.device_in") device_in_id = kv.first;
            else if (kv.second.component == "orpheus.builtin.device_out") device_out_id = kv.first;
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
        host.rb = nullptr;

        std::string chs = get_param(plan, device_in_id, "channels");
        if (!chs.empty()) host.channels = (uint32_t)std::atoi(chs.c_str());

        std::string in_device = get_param(plan, device_in_id, "device");
        std::string out_device = get_param(plan, device_out_id, "device");
        std::string source = get_param(plan, device_in_id, "source", "microphone");
        bool loopback = (source == "loopback");

        ma_device_id in_id, out_id;
        ma_device_id* p_in_id = find_device_id(
            loopback ? ma_device_type_playback : ma_device_type_capture, in_device, &in_id) ? &in_id : nullptr;
        ma_device_id* p_out_id = find_device_id(ma_device_type_playback, out_device, &out_id) ? &out_id : nullptr;
        if (!in_device.empty() && !p_in_id) {
            std::cerr << "Input device not found: " << in_device << std::endl;
            return 1;
        }
        if (!out_device.empty() && !p_out_id) {
            std::cerr << "Output device not found: " << out_device << std::endl;
            return 1;
        }

        if (!loopback) {
            // microphone/line-in: one duplex device
            ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
            cfg.capture.pDeviceID = p_in_id;
            cfg.capture.format = ma_format_f32;
            cfg.capture.channels = host.channels;
            cfg.playback.pDeviceID = p_out_id;
            cfg.playback.format = ma_format_f32;
            cfg.playback.channels = host.channels;
            cfg.sampleRate = sample_rate;
            cfg.dataCallback = data_callback;
            cfg.pUserData = &host;
            if (ma_device_init(NULL, &cfg, &play_device) != MA_SUCCESS) {
                std::cerr << "Failed to initialize audio device" << std::endl;
                return 1;
            }
            play_inited = true;
            if (ma_device_start(&play_device) != MA_SUCCESS) {
                std::cerr << "Failed to start audio device" << std::endl;
                ma_device_uninit(&play_device);
                return 1;
            }
        } else {
            // loopback: capture system mix into ring buffer, playback device drives the graph
            if (ma_pcm_rb_init(ma_format_f32, host.channels, sample_rate / 10, NULL, NULL, &rb) != MA_SUCCESS) {
                std::cerr << "Failed to init ring buffer" << std::endl;
                return 1;
            }
            rb_inited = true;
            host.rb = &rb;

            ma_device_config cap_cfg = ma_device_config_init(ma_device_type_loopback);
            cap_cfg.capture.pDeviceID = p_in_id;  // loopback target = playback device to tap
            cap_cfg.capture.format = ma_format_f32;
            cap_cfg.capture.channels = host.channels;
            cap_cfg.sampleRate = sample_rate;
            cap_cfg.dataCallback = loopback_capture_callback;
            cap_cfg.pUserData = &host;
            if (ma_device_init(NULL, &cap_cfg, &cap_device) != MA_SUCCESS) {
                std::cerr << "Failed to initialize loopback capture" << std::endl;
                ma_pcm_rb_uninit(&rb);
                return 1;
            }
            cap_inited = true;

            ma_device_config play_cfg = ma_device_config_init(ma_device_type_playback);
            play_cfg.playback.pDeviceID = p_out_id;
            play_cfg.playback.format = ma_format_f32;
            play_cfg.playback.channels = host.channels;
            play_cfg.sampleRate = sample_rate;
            play_cfg.dataCallback = loopback_playback_callback;
            play_cfg.pUserData = &host;
            if (ma_device_init(NULL, &play_cfg, &play_device) != MA_SUCCESS) {
                std::cerr << "Failed to initialize playback device" << std::endl;
                ma_device_uninit(&cap_device);
                ma_pcm_rb_uninit(&rb);
                return 1;
            }
            play_inited = true;

            if (ma_device_start(&play_device) != MA_SUCCESS ||
                ma_device_start(&cap_device) != MA_SUCCESS) {
                std::cerr << "Failed to start audio devices" << std::endl;
                ma_device_uninit(&play_device);
                ma_device_uninit(&cap_device);
                ma_pcm_rb_uninit(&rb);
                return 1;
            }
        }

        std::cout << "Real-time audio running (" << (loopback ? "loopback" : "microphone")
                  << "). Press Enter to stop..." << std::endl;
        std::cin.get();

        if (play_inited) ma_device_uninit(&play_device);
        if (cap_inited) ma_device_uninit(&cap_device);
        if (rb_inited) ma_pcm_rb_uninit(&rb);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        if (play_inited) ma_device_uninit(&play_device);
        if (cap_inited) ma_device_uninit(&cap_device);
        if (rb_inited) ma_pcm_rb_uninit(&rb);
        return 1;
    }

    return 0;
}
