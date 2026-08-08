#include "orpheus_runtime/plan.h"
#include "orpheus_runtime/runtime.h"
#include "orpheus_runtime/wav_io.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <plan.json> <component_dir> [--pace] [--probe-interval ms]"
              << "\n       " << prog << " <plan.json> <component_dir> --map"
              << "\n       " << prog << " <plan.json> <component_dir> --resolve <id|0xhex>" << std::endl;
}

static const char* id_kind_name(uint32_t kind) {
    switch (kind) {
        case ORPHEUS_ID_RTC: return "RTC";
        case ORPHEUS_ID_TUNE: return "TUNE";
        case ORPHEUS_ID_PROBE: return "PROBE";
        case ORPHEUS_ID_STATE: return "STATE";
        case ORPHEUS_ID_CUSTOM: return "CUSTOM";
        default: return "RESERVED";
    }
}

static const char* data_form_name(uint32_t form) {
    switch (form) {
        case ORPHEUS_FORM_SCALAR: return "SCALAR";
        case ORPHEUS_FORM_BULK: return "BULK";
        case ORPHEUS_FORM_MODULE: return "MODULE";
        default: return "?";
    }
}

/* 内存透明：ID → 类型/长度/基址/偏移（供控制/调试查询） */
static void print_resolved(const OrpheusResolvedData& d) {
    std::cout << "RESOLVED " << std::hex << "0x" << d.id << std::dec
              << " " << id_kind_name(d.kind)
              << " " << data_form_name(d.form)
              << " type=" << d.type
              << " count=" << d.count
              << " bytes=" << d.byte_size
              << " module=" << d.module_id
              << " slot=" << d.slot
              << " base=" << static_cast<const void*>(d.base)
              << " offset=" << d.offset
              << " node=" << (d.node ? d.node : "")
              << " key=" << (d.key ? d.key : "")
              << " name=" << (d.name ? d.name : "") << std::endl;
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

/* 上报全部 PROBE 槽（离线一次性 / 按真实时长播放时周期调用） */
static void dump_probes(orpheus::Runtime& runtime, const orpheus::Plan& plan) {
    for (const auto& node_id : plan.execution_order) {
        auto slots = runtime.probe_slots(node_id);
        for (const orpheus::SlotEntry* e : slots) {
            OrpheusValue v;
            if (runtime.get_parameter(node_id, e->key, &v) != ORPHEUS_OK) continue;
            if (v.type == ORPHEUS_VALUE_FLOAT) {
                std::cout << "PROBE " << node_id << " " << e->key << " " << v.value.f32 << std::endl;
            } else if (v.type == ORPHEUS_VALUE_INT) {
                std::cout << "PROBE " << node_id << " " << e->key << " " << v.value.i32 << std::endl;
            } else if (v.type == ORPHEUS_VALUE_STRING) {
                std::cout << "PROBE_JSON " << node_id << " " << e->key << " " << v.value.str << std::endl;
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string plan_path = argv[1];
    std::string component_dir = argv[2];
    bool pace = false;
    uint32_t probe_interval_ms = 0;
    bool dump_map = false;
    bool have_resolve = false;
    uint32_t resolve_id = 0;
    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "--pace") {
            pace = true;
        } else if (std::string(argv[i]) == "--probe-interval" && i + 1 < argc) {
            probe_interval_ms = (uint32_t)std::atoi(argv[++i]);
        } else if (std::string(argv[i]) == "--map") {
            dump_map = true;
        } else if (std::string(argv[i]) == "--resolve" && i + 1 < argc) {
            resolve_id = (uint32_t)std::strtoul(argv[++i], nullptr, 0);
            have_resolve = true;
        }
    }

    try {
        orpheus::Plan plan = orpheus::Plan::load_from_file(plan_path);

        orpheus::Runtime runtime;
        int rc = runtime.load_plan(plan, component_dir);
        if (rc != 0) {
            std::cerr << "Failed to load plan: " << rc << std::endl;
            return 1;
        }

        if (dump_map) {
            std::vector<OrpheusResolvedData> all;
            runtime.resolve_all(&all);
            for (const auto& d : all) print_resolved(d);
            return 0;
        }
        if (have_resolve) {
            OrpheusResolvedData d;
            if (runtime.resolve(resolve_id, &d) == ORPHEUS_OK) {
                print_resolved(d);
            } else {
                std::cout << "ERR RESOLVE " << std::hex << "0x" << resolve_id << std::dec << std::endl;
            }
            return 0;
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
            total_frames = plan.duration_frames > 0
                ? plan.duration_frames
                : plan.sample_rate * 10;  // 10 seconds default（按图采样率）
        }

        uint32_t block_size = plan.block_size;
        uint32_t processed = 0;
        std::atomic<bool> probes_running{true};
        std::thread probe_thread;
        if (probe_interval_ms > 0) {
            probe_thread = std::thread([&]() {
                while (probes_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(probe_interval_ms));
                    if (!probes_running) break;
                    dump_probes(runtime, plan);
                }
            });
        }
        auto t_start = std::chrono::steady_clock::now();
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
            if (pace) {
                /* 按真实时长播放：处理完的音频时长 ≈ 墙钟流逝时间 */
                double target_ms = (double)processed / (double)plan.sample_rate * 1000.0;
                double elapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t_start).count();
                double slp = target_ms - elapsed;
                if (slp > 1.0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds((long)slp));
                }
            }
        }
        probes_running = false;
        if (probe_thread.joinable()) probe_thread.join();

        std::cout << "Processed " << processed << " frames" << std::endl;
        dump_probes(runtime, plan);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
