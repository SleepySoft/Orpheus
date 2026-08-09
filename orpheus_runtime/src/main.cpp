#include "orpheus_runtime/plan.h"
#include "orpheus_runtime/runtime.h"
#include "orpheus_runtime/wav_io.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <plan.json> <component_dir> [--pace] [--probe-interval ms]"
              << "\n       " << prog << " <plan.json> <component_dir> --map"
              << "\n       " << prog << " <plan.json> <component_dir> --resolve <id|0xhex>"
              << "\n       " << prog << " <plan.json> <component_dir> --rw <id> <value> | --rr <id>"
              << "\n       " << prog << " <plan.json> <component_dir> --rwb <id> <n> <v0>... [--run <blocks>] [--rgb <id>]"
              << "\n       " << prog << " <plan.json> <component_dir> --getbulk <node> <key>"
              << "\n       " << prog << " <plan.json> <component_dir> [--echo-hook <id>] --msg <hex>" << std::endl;
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

static std::string to_hex(const uint8_t* p, size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s.push_back(digits[p[i] >> 4]);
        s.push_back(digits[p[i] & 0xF]);
    }
    return s;
}

static bool from_hex(const std::string& hx, std::vector<uint8_t>* out) {
    if (hx.size() % 2 != 0) return false;
    out->clear();
    auto cv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < hx.size(); i += 2) {
        int hi = cv(hx[i]), lo = cv(hx[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out->push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

/* 测试用 echo hook：把请求 payload 原样作为响应返回（CUSTOM/消息路径验证）。 */
static int echo_hook(void* ctx, uint32_t id, uint32_t event,
                     const OrpheusBlob* req, OrpheusBlob* resp) {
    (void)ctx; (void)id; (void)event;
    if (resp == nullptr) return ORPHEUS_HOOK_HANDLED;  /* notification：处理但不返回 */
    if (req != nullptr && req->len > 0) {
        std::memcpy(const_cast<void*>(resp->data), req->data, req->len);
        resp->len = req->len;
    } else {
        resp->len = 0;
    }
    return ORPHEUS_HOOK_HANDLED;
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
    bool have_rw = false, have_rr = false, have_rwb = false;
    uint32_t rw_id = 0, rr_id = 0, rwb_id = 0;
    std::string rw_value;
    std::vector<float> rwb_vals;
    uint32_t run_blocks = 0;
    bool have_gb = false, have_rgb = false;
    std::string gb_node, gb_key;
    uint32_t rgb_id = 0;
    bool have_echo = false, have_msg = false;
    uint32_t echo_id = 0;
    std::vector<std::pair<std::string, std::string>> actions;  // ("msg", hex) / ("run", n)，按命令行顺序执行
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
        } else if (std::string(argv[i]) == "--rw" && i + 2 < argc) {
            rw_id = (uint32_t)std::strtoul(argv[++i], nullptr, 0);
            rw_value = argv[++i];
            have_rw = true;
        } else if (std::string(argv[i]) == "--rr" && i + 1 < argc) {
            rr_id = (uint32_t)std::strtoul(argv[++i], nullptr, 0);
            have_rr = true;
        } else if (std::string(argv[i]) == "--rwb" && i + 2 < argc) {
            rwb_id = (uint32_t)std::strtoul(argv[++i], nullptr, 0);
            size_t n = (size_t)std::atoi(argv[++i]);
            for (size_t k = 0; k < n && i + 1 < argc; ++k) {
                rwb_vals.push_back((float)std::atof(argv[++i]));
            }
            have_rwb = true;
        } else if (std::string(argv[i]) == "--run" && i + 1 < argc) {
            run_blocks = (uint32_t)std::atoi(argv[++i]);
            actions.push_back({"run", std::to_string(run_blocks)});
        } else if (std::string(argv[i]) == "--getbulk" && i + 2 < argc) {
            gb_node = argv[++i];
            gb_key = argv[++i];
            have_gb = true;
        } else if (std::string(argv[i]) == "--rgb" && i + 1 < argc) {
            rgb_id = (uint32_t)std::strtoul(argv[++i], nullptr, 0);
            have_rgb = true;
        } else if (std::string(argv[i]) == "--echo-hook" && i + 1 < argc) {
            echo_id = (uint32_t)std::strtoul(argv[++i], nullptr, 0);
            have_echo = true;
        } else if (std::string(argv[i]) == "--msg" && i + 1 < argc) {
            actions.push_back({"msg", argv[++i]});
            have_msg = true;
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
        if (have_echo) {
            runtime.register_hook(echo_id, echo_hook, nullptr);
        }
        if (have_msg) {
            for (const auto& act : actions) {
                if (act.first == "run") {
                    int n = std::atoi(act.second.c_str());
                    for (int i = 0; i < n; ++i) {
                        if (runtime.process_block(plan.block_size) != 0) return 1;
                    }
                    continue;
                }
                std::vector<uint8_t> in;
                if (!from_hex(act.second, &in)) {
                    std::cout << "ERR MSG hex" << std::endl;
                    return 1;
                }
                uint8_t out[65536];
                size_t out_len = 0;
                if (runtime.message(in.data(), in.size(), out, sizeof(out), &out_len) != 0) {
                    std::cout << "ERR MSG dispatch" << std::endl;
                    return 1;
                }
                if (out_len == 0) {
                    std::cout << "MSGNONE" << std::endl;
                } else {
                    std::cout << "MSGRSP " << to_hex(out, out_len) << std::endl;
                }
            }
            return 0;
        }
        bool did_id_cmd = false;
        if (have_rw) {
            OrpheusValue v;
            char* end = nullptr;
            float f = std::strtof(rw_value.c_str(), &end);
            if (end != rw_value.c_str() && end && *end == '\0') {
                v.type = ORPHEUS_VALUE_FLOAT;
                v.value.f32 = f;
            } else {
                v.type = ORPHEUS_VALUE_STRING;
                v.value.str = rw_value.c_str();
            }
            int r = runtime.write_id(rw_id, v);
            std::cout << (r == ORPHEUS_OK ? "OK RW " : "ERR RW ")
                      << std::hex << "0x" << rw_id << std::dec << std::endl;
            did_id_cmd = true;
        }
        if (have_rwb) {
            int r = runtime.write_bulk_id(rwb_id, rwb_vals.data(), rwb_vals.size());
            std::cout << (r == ORPHEUS_OK ? "OK RWB " : "ERR RWB ")
                      << std::hex << "0x" << rwb_id << std::dec << std::endl;
            did_id_cmd = true;
        }
        if (run_blocks > 0 && did_id_cmd) {
            for (uint32_t i = 0; i < run_blocks; ++i) {
                if (runtime.process_block(plan.block_size) != 0) return 1;
            }
        }
        if (have_rr) {
            OrpheusValue v;
            int r = runtime.read_id(rr_id, &v);
            if (r == ORPHEUS_OK) {
                if (v.type == ORPHEUS_VALUE_FLOAT)
                    std::cout << "RVALUE " << std::hex << "0x" << rr_id << std::dec
                              << " " << v.value.f32 << std::endl;
                else if (v.type == ORPHEUS_VALUE_INT)
                    std::cout << "RVALUE " << std::hex << "0x" << rr_id << std::dec
                              << " " << v.value.i32 << std::endl;
                else if (v.type == ORPHEUS_VALUE_STRING)
                    std::cout << "RVALUE " << std::hex << "0x" << rr_id << std::dec
                              << " " << v.value.str << std::endl;
                else
                    std::cout << "RVALUE " << std::hex << "0x" << rr_id << std::dec << " ?" << std::endl;
            } else {
                std::cout << "ERR RR " << std::hex << "0x" << rr_id << std::dec << std::endl;
            }
            did_id_cmd = true;
        }
        if (have_gb || have_rgb) {
            uint32_t id = 0;
            bool have_id = false;
            if (have_rgb) {
                id = rgb_id;
                have_id = true;
            } else {
                have_id = runtime.lookup_id(gb_node, gb_key, &id);
            }
            OrpheusResolvedData d;
            if (!have_id || runtime.resolve(id, &d) != ORPHEUS_OK ||
                d.form != ORPHEUS_FORM_BULK) {
                std::cout << "ERR GETBULK " << std::hex << "0x" << id << std::dec << std::endl;
            } else {
                std::vector<float> vals(d.count);
                if (runtime.get_bulk_id(id, vals.data(), vals.size()) == ORPHEUS_OK) {
                    std::cout << "BULKVALUE " << std::hex << "0x" << id << std::dec;
                    for (const float v : vals) std::cout << " " << v;
                    std::cout << std::endl;
                } else {
                    std::cout << "ERR GETBULK " << std::hex << "0x" << id << std::dec << std::endl;
                }
            }
            did_id_cmd = true;
        }
        if (did_id_cmd) return 0;

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
