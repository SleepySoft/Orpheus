#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "orpheus_runtime/plan.h"
#include "orpheus_runtime/runtime.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

struct HostContext {
    orpheus::Runtime* runtime;
    OrpheusBuffer* device_in_buf;
    OrpheusBuffer* device_out_buf;
    uint32_t in_channels;   // capture side channels (device_in port)
    uint32_t out_channels;  // playback side channels (device_out port)
    uint32_t buffer_size;   // async ring buffer capacity in frames (0 = auto: sample_rate/10)
    uint32_t block_size;  // graph buffers are sized for this; chunk larger device periods
    ma_pcm_rb* rb;        // async bridge: ring buffer from capture to playback device
    uint32_t rb_capacity;   // ring buffer capacity in frames (0 if no async bridge)
    std::atomic<uint32_t> underruns{0};  // playback starved (rb empty)
    std::atomic<uint32_t> overruns{0};   // capture dropped (rb full, clock drift)
    std::atomic<bool> primed{false};     // async bridge: capture pre-filled the cushion?
    uint32_t prime_target{0};             // frames the buffer must reach before playback consumes
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

// Query device native formats and report whether the requested channels and
// sample rate are natively supported (0), require miniaudio conversion (1), or
// could not be determined (2). msg receives a human-readable summary.
static int check_device_caps(ma_device_type type, const ma_device_id* pId,
                             uint32_t channels, uint32_t sample_rate,
                             std::string& msg) {
    msg.clear();
    ma_context ctx;
    if (ma_context_init(NULL, 0, NULL, &ctx) != MA_SUCCESS) {
        msg = "cannot init audio context for capability query";
        return 2;
    }

    ma_device_id default_id;
    const ma_device_id* query_id = pId;
    if (query_id == nullptr) {
        ma_device_info* pPlay = nullptr;
        ma_device_info* pCap = nullptr;
        ma_uint32 nPlay = 0, nCap = 0;
        if (ma_context_get_devices(&ctx, &pPlay, &nPlay, &pCap, &nCap) == MA_SUCCESS) {
            ma_device_info* infos = (type == ma_device_type_playback) ? pPlay : pCap;
            ma_uint32 count = (type == ma_device_type_playback) ? nPlay : nCap;
            for (ma_uint32 i = 0; i < count; ++i) {
                if (infos[i].isDefault) {
                    default_id = infos[i].id;
                    query_id = &default_id;
                    break;
                }
            }
        }
    }

    ma_device_info info;
    ma_result r = ma_context_get_device_info(&ctx, type, query_id, &info);
    ma_context_uninit(&ctx);
    if (r != MA_SUCCESS) {
        msg = "device capability info unavailable";
        return 2;
    }

    std::string devname = info.name;
    if (info.nativeDataFormatCount == 0) {
        msg = devname + ": no native format info (conversion handled automatically)";
        return 2;
    }

    bool native = false, ch_any = false, rate_any = false;
    for (ma_uint32 i = 0; i < info.nativeDataFormatCount; ++i) {
        const auto& f = info.nativeDataFormats[i];
        bool e_ch = (f.channels == 0 || f.channels == channels);
        bool e_rate = (f.sampleRate == 0 || f.sampleRate == sample_rate);
        if (e_ch) ch_any = true;
        if (e_rate) rate_any = true;
        if (e_ch && e_rate) { native = true; break; }
    }

    if (native) {
        msg = devname + ": native support";
        return 0;
    }
    msg = devname + ": device will convert (";
    if (!ch_any && !rate_any) {
        msg += "channels " + std::to_string(channels) + ", rate " + std::to_string(sample_rate) + "Hz";
    } else if (!ch_any) {
        msg += "channels -> " + std::to_string(channels);
    } else {
        msg += "rate -> " + std::to_string(sample_rate) + "Hz";
    }
    msg += ")";
    return 1;
}

// ---------------------------------------------------------------- callbacks

// duplex (microphone) mode: capture+playback in one device.
// The device period may exceed the graph block size -> process in chunks.
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    HostContext* host = (HostContext*)pDevice->pUserData;
    if (!host || !host->runtime) return;

    float* out = (float*)pOutput;
    const float* in = (const float*)pInput;
    uint32_t in_ch = host->in_channels;
    uint32_t out_ch = host->out_channels;
    uint32_t bs = host->block_size > 0 ? host->block_size : frameCount;

    for (ma_uint32 done = 0; done < frameCount; done += bs) {
        uint32_t n = (uint32_t)((frameCount - done) < bs ? (frameCount - done) : bs);
        if (host->device_in_buf && in) {
            std::memcpy(host->device_in_buf->data, in + (size_t)done * in_ch, n * in_ch * sizeof(float));
            host->device_in_buf->frame_count = n;
        }
        host->runtime->process_block(n);
        if (host->device_out_buf && out) {
            std::memcpy(out + (size_t)done * out_ch, host->device_out_buf->data, n * out_ch * sizeof(float));
        } else if (out) {
            std::memset(out + (size_t)done * out_ch, 0, n * out_ch * sizeof(float));
        }
    }
}

// async bridge: capture side pushes input into the ring buffer (any capture source)
void rb_capture_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pOutput;
    HostContext* host = (HostContext*)pDevice->pUserData;
    if (!host || !host->rb || !pInput) return;
    ma_uint32 writable = frameCount;
    void* w = nullptr;
    if (ma_pcm_rb_acquire_write(host->rb, &writable, &w) == MA_SUCCESS && writable > 0) {
        std::memcpy(w, pInput, writable * host->in_channels * sizeof(float));
        ma_pcm_rb_commit_write(host->rb, writable);
        if (writable < frameCount) host->overruns++;
    } else {
        host->overruns++;
    }
}

// async bridge: playback side is the master clock; pull input from the ring buffer
void rb_playback_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    HostContext* host = (HostContext*)pDevice->pUserData;
    if (!host || !host->runtime) return;
    float* out = (float*)pOutput;
    uint32_t in_ch = host->in_channels;
    uint32_t out_ch = host->out_channels;
    uint32_t bs = host->block_size > 0 ? host->block_size : frameCount;

    // Priming: the ring buffer starts empty. Wait for capture to pre-fill a cushion
    // (prime_target frames) before playback consumes, otherwise playback starves on a
    // near-empty buffer and chronic underruns occur even when capture/playback rates match.
    if (host->rb && host->prime_target > 0 && !host->primed.load(std::memory_order_relaxed)) {
        if (ma_pcm_rb_available_read(host->rb) < host->prime_target) {
            std::memset(out, 0, (size_t)frameCount * out_ch * sizeof(float));
            return;
        }
        host->primed.store(true, std::memory_order_relaxed);
    }

    for (ma_uint32 done = 0; done < frameCount; done += bs) {
        uint32_t n = (uint32_t)((frameCount - done) < bs ? (frameCount - done) : bs);
        if (host->device_in_buf) {
            float* buf = (float*)host->device_in_buf->data;
            ma_uint32 readable = n;
            void* r = nullptr;
            ma_uint32 got = 0;
            if (host->rb && ma_pcm_rb_acquire_read(host->rb, &readable, &r) == MA_SUCCESS && readable > 0) {
                std::memcpy(buf, r, readable * in_ch * sizeof(float));
                ma_pcm_rb_commit_read(host->rb, readable);
                got = readable;
            }
            if (got < n) {
                host->underruns++;
                std::memset(buf + (size_t)got * in_ch, 0, (n - got) * in_ch * sizeof(float));
            }
            host->device_in_buf->frame_count = n;
        }
        host->runtime->process_block(n);
        if (host->device_out_buf && out) {
            std::memcpy(out + (size_t)done * out_ch, host->device_out_buf->data, n * out_ch * sizeof(float));
        } else if (out) {
            std::memset(out + (size_t)done * out_ch, 0, n * out_ch * sizeof(float));
        }
    }
}

// ---------------------------------------------------------------- control

// Print readback values of all probe nodes: PROBE <node> <param> <value>
static void report_probes(orpheus::Runtime& runtime, const orpheus::Plan& plan) {
    for (const auto& node_id : plan.execution_order) {
        // v2：探针发现统一走注册表（PROBE 槽），不再按组件名 ".probe" 猜测
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
}

// stdin control protocol (one command per line):
//   SET <node> <param> <value>   -> runtime.set_parameter (numeric -> float, else string)
//   GET <node> <param>           -> prints VALUE <node> <param> <value>
//   BULK <node> <key> <n> <v0>...-> runtime.write_bulk (BULK 槽直写，如 biquad_bank 系数)
//   STOP (or empty line / EOF)   -> shut down
static void control_loop(orpheus::Runtime& runtime, std::atomic<bool>& running) {
    std::string line;
    while (running && std::getline(std::cin, line)) {
        if (line.empty()) break;  // Enter = stop
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd == "STOP") {
            break;
        } else if (cmd == "SET") {
            std::string node, param, raw;
            iss >> node >> param;
            std::getline(iss, raw);
            if (!raw.empty() && raw[0] == ' ') raw.erase(0, 1);
            OrpheusValue v;
            char* end = nullptr;
            float f = std::strtof(raw.c_str(), &end);
            std::string storage = raw;
            if (end != raw.c_str() && end && *end == '\0') {
                v.type = ORPHEUS_VALUE_FLOAT;
                v.value.f32 = f;
            } else {
                v.type = ORPHEUS_VALUE_STRING;
                v.value.str = storage.c_str();
            }
            int r = runtime.set_parameter(node, param, v);
            std::cout << (r == ORPHEUS_OK ? "OK SET " : "ERR SET ") << node << " " << param << std::endl;
        } else if (cmd == "GET") {
            std::string node, param;
            iss >> node >> param;
            OrpheusValue v;
            int r = runtime.get_parameter(node, param, &v);
            if (r == ORPHEUS_OK) {
                if (v.type == ORPHEUS_VALUE_FLOAT)
                    std::cout << "VALUE " << node << " " << param << " " << v.value.f32 << std::endl;
                else if (v.type == ORPHEUS_VALUE_INT)
                    std::cout << "VALUE " << node << " " << param << " " << v.value.i32 << std::endl;
                else if (v.type == ORPHEUS_VALUE_STRING)
                    std::cout << "VALUE " << node << " " << param << " " << v.value.str << std::endl;
            } else {
                std::cout << "ERR GET " << node << " " << param << std::endl;
            }
        } else if (cmd == "BULK") {
            std::string node, key;
            size_t n = 0;
            iss >> node >> key >> n;
            std::vector<float> vals;
            vals.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                float v = 0.0f;
                if (!(iss >> v)) break;
                vals.push_back(v);
            }
            if (vals.size() != n) {
                std::cout << "ERR BULK " << node << " " << key << std::endl;
                continue;
            }
            int r = runtime.write_bulk(node, key, vals.data(), vals.size());
            std::cout << (r == ORPHEUS_OK ? "OK BULK " : "ERR BULK ") << node << " " << key << std::endl;
        }
    }
    running = false;
}

// ---------------------------------------------------------------- main

// Graph block sizes can be tiny (e.g. 128 frames = 2.7ms), which is too
// aggressive for shared-mode devices (and impossible for Bluetooth). The
// device period is decoupled from the graph block size (callbacks already
// chunk large periods into block_size steps), so request a sane period.
static ma_uint32 device_period_frames(uint32_t sample_rate, uint32_t block_size) {
    ma_uint32 floor_frames = sample_rate / 100;  // 10ms
    return block_size > floor_frames ? block_size : floor_frames;
}

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <plan.json> <component_dir> [sample_rate] [block_size]\n"
              << "       " << prog << " --list-devices" << std::endl;
}

int main(int argc, char** argv) {
    // unbuffered stdout: log/probe lines must reach the parent process immediately
    setvbuf(stdout, NULL, _IONBF, 0);
    std::cout << std::unitbuf;

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
        bool has_in = !device_in_id.empty();
        bool has_out = !device_out_id.empty();
        if (!has_in && !has_out) {
            std::cerr << "Plan has no device_in/device_out nodes; "
                         "use orpheus_runtime (file-clocked host) instead" << std::endl;
            return 1;
        }

        HostContext host;
        host.runtime = &runtime;
        host.device_in_buf = has_in ? runtime.get_output_buffer(device_in_id, "out") : nullptr;
        host.device_out_buf = has_out ? runtime.get_input_buffer(device_out_id, "in") : nullptr;
        host.in_channels = 2;
        host.out_channels = 2;
        host.block_size = block_size;
        host.buffer_size = plan.buffer_size;
        host.rb = nullptr;

        if (has_in) {
            std::string chs = get_param(plan, device_in_id, "channels");
            if (!chs.empty()) host.in_channels = (uint32_t)std::atoi(chs.c_str());
        }
        if (has_out) {
            std::string chs = get_param(plan, device_out_id, "channels");
            if (!chs.empty()) host.out_channels = (uint32_t)std::atoi(chs.c_str());
        }

        std::string in_device = has_in ? get_param(plan, device_in_id, "device") : "";
        std::string out_device = has_out ? get_param(plan, device_out_id, "device") : "";
        std::string source = has_in ? get_param(plan, device_in_id, "source", "microphone") : "";
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

        // Validate device capabilities: warn when miniaudio will convert; a true
        // unsupported configuration surfaces as an init failure below (error).
        if (has_in) {
            std::string caps;
            int cs = check_device_caps(loopback ? ma_device_type_playback : ma_device_type_capture,
                                       p_in_id, host.in_channels, sample_rate, caps);
            if (cs == 1) std::cout << "LOG WARN input device will convert: " << caps << std::endl;
            else if (cs == 0) std::cout << "LOG input device native: " << caps << std::endl;
            else std::cout << "LOG input device caps unknown: " << caps << std::endl;
        }
        if (has_out) {
            std::string caps;
            int cs = check_device_caps(ma_device_type_playback, p_out_id,
                                       host.out_channels, sample_rate, caps);
            if (cs == 1) std::cout << "LOG WARN output device will convert: " << caps << std::endl;
            else if (cs == 0) std::cout << "LOG output device native: " << caps << std::endl;
            else std::cout << "LOG output device caps unknown: " << caps << std::endl;
        }

        // Device topology by graph content (IO is the graph's business):
        //   in+out, both default devices: one duplex device (same clock domain, lowest latency)
        //   in+out, any explicit device or loopback: async bridge (capture -> ring buffer ->
        //     playback master clock) — decouples mismatched device clocks (e.g. virtual cable
        //     + headphones) and enables under/overrun detection via ring buffer water level
        //   out only: playback device clock (e.g. wav_in -> speakers)
        //   in only:  capture/loopback device clock (e.g. system audio -> wav_out)
        bool async_bridge = has_in && has_out && (loopback || !in_device.empty() || !out_device.empty());
        if (has_in && has_out && !async_bridge) {
            ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
            cfg.capture.pDeviceID = p_in_id;
            cfg.capture.format = ma_format_f32;
            cfg.capture.channels = host.in_channels;
            cfg.playback.pDeviceID = p_out_id;
            cfg.playback.format = ma_format_f32;
            cfg.playback.channels = host.out_channels;
            cfg.sampleRate = sample_rate;
            cfg.periodSizeInFrames = device_period_frames(sample_rate, block_size);
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
        } else if (has_in && has_out) {
            // async bridge (covers loopback and mismatched capture/playback devices)
            uint32_t rb_frames = host.buffer_size ? host.buffer_size : (sample_rate / 10);
            if (ma_pcm_rb_init(ma_format_f32, host.in_channels, rb_frames, NULL, NULL, &rb) != MA_SUCCESS) {
                std::cerr << "Failed to init ring buffer" << std::endl;
                return 1;
            }
            rb_inited = true;
            host.rb = &rb;
            host.rb_capacity = rb_frames;
            host.prime_target = rb_frames / 3;  // pre-fill cushion before playback consumes

            ma_device_config cap_cfg = ma_device_config_init(
                loopback ? ma_device_type_loopback : ma_device_type_capture);
            cap_cfg.capture.pDeviceID = p_in_id;  // loopback target = playback device to tap
            cap_cfg.capture.format = ma_format_f32;
            cap_cfg.capture.channels = host.in_channels;
            cap_cfg.sampleRate = sample_rate;
            cap_cfg.periodSizeInFrames = device_period_frames(sample_rate, block_size);
            cap_cfg.dataCallback = rb_capture_callback;
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
            play_cfg.playback.channels = host.out_channels;
            play_cfg.sampleRate = sample_rate;
            play_cfg.periodSizeInFrames = device_period_frames(sample_rate, block_size);
            play_cfg.dataCallback = rb_playback_callback;
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
        } else {
            // single device: it is the clock; data_callback handles null in/out
            ma_device_config cfg = ma_device_config_init(
                has_out ? ma_device_type_playback
                        : (loopback ? ma_device_type_loopback : ma_device_type_capture));
            if (has_out) {
                cfg.playback.pDeviceID = p_out_id;
                cfg.playback.format = ma_format_f32;
                cfg.playback.channels = host.out_channels;
            } else {
                cfg.capture.pDeviceID = p_in_id;
                cfg.capture.format = ma_format_f32;
                cfg.capture.channels = host.in_channels;
            }
            cfg.sampleRate = sample_rate;
            cfg.periodSizeInFrames = device_period_frames(sample_rate, block_size);
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
        }

        std::cout << "LOG rt_host running (in="
                  << (has_in ? (loopback ? "loopback" : "mic") : "none")
                  << ", out=" << (has_out ? "playback" : "none")
                  << ", mode=" << (async_bridge ? "async-bridge" : (cap_inited || play_inited) ? "device-clock" : "duplex")
                  << ", in_channels=" << host.in_channels
                  << ", out_channels=" << host.out_channels
                  << ", sample_rate=" << sample_rate
                  << ", block_size=" << block_size << ")" << std::endl;
        if (play_inited) {
            std::cout << "LOG device period: playback="
                      << play_device.playback.internalPeriodSizeInFrames << " frames";
            if (cap_inited) {
                std::cout << ", capture=" << cap_device.capture.internalPeriodSizeInFrames << " frames";
            } else if (has_in) {
                std::cout << ", capture=" << play_device.capture.internalPeriodSizeInFrames << " frames";
            }
            std::cout << std::endl;
        }

        std::atomic<bool> running{true};
        std::thread probe_thread([&]() {
            uint32_t last_u = 0, last_o = 0;
            int ticks = 0;
            bool priming_warned = false, primed_logged = false;
            while (running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (!running) break;
                report_probes(runtime, plan);
                // ring-buffer water level + over/underrun counts -> UI gauge (PROBE_JSON __host__)
                {
                    uint32_t lvl = host.rb ? ma_pcm_rb_available_read(host.rb) : 0;
                    uint32_t cap = host.rb_capacity;
                    std::cout << "PROBE_JSON __host__ rb {\"level\":" << lvl
                              << ",\"capacity\":" << cap
                              << ",\"primed\":" << (host.primed.load() ? "true" : "false")
                              << ",\"underruns\":" << host.underruns.load()
                              << ",\"overruns\":" << host.overruns.load()
                              << ",\"bridge\":" << (host.rb ? "true" : "false")
                              << "}" << std::endl;
                }
                // once per second: report ring-buffer water level problems with advice
                if (++ticks % 5 == 0) {
                    uint32_t u = host.underruns.load();
                    uint32_t o = host.overruns.load();
                    if (u != last_u) {
                        std::cout << "LOG WARN 播放欠载 x" << (u - last_u)
                                  << "/s：播放设备取数不足（出现杂音/哒哒声）。建议：增大 buffer_size，"
                                     "或检查采集设备是否正常供数/改用同一设备时钟" << std::endl;
                    }
                    if (o != last_o) {
                        std::cout << "LOG WARN 采集溢出 x" << (o - last_o)
                                  << "/s：输入数据堆积被丢弃（采集与播放时钟漂移）。"
                                     "建议：增大 buffer_size，或让输入输出共用同一设备/时钟" << std::endl;
                    }
                    // priming status (async bridge only)
                    if (host.rb) {
                        if (!host.primed.load()) {
                            if (!priming_warned && ticks >= 10) {
                                priming_warned = true;
                                std::cout << "LOG WARN 缓冲预充不足：采集 2 秒内未填满水位（loopback 目标可能未在播放，或采集设备异常）。播放暂输出静音，等待采集供数。" << std::endl;
                            }
                        } else if (!primed_logged) {
                            primed_logged = true;
                            std::cout << "LOG 缓冲预充完成，开始播放" << std::endl;
                        }
                    }
                    last_u = u;
                    last_o = o;
                }
            }
        });

        control_loop(runtime, running);  // returns on STOP / Enter / stdin EOF

        running = false;
        if (probe_thread.joinable()) probe_thread.join();

        if (play_inited) ma_device_uninit(&play_device);
        if (cap_inited) ma_device_uninit(&cap_device);
        if (rb_inited) ma_pcm_rb_uninit(&rb);
        std::cout << "LOG rt_host stopped" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        if (play_inited) ma_device_uninit(&play_device);
        if (cap_inited) ma_device_uninit(&cap_device);
        if (rb_inited) ma_pcm_rb_uninit(&rb);
        return 1;
    }

    return 0;
}
