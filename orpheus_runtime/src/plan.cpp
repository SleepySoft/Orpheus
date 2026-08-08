#include "orpheus_runtime/plan.h"

#include "orpheus_abi.h"

#include "json.hpp"

#include <fstream>

using json = nlohmann::json;

namespace orpheus {

Plan Plan::load_from_file(const std::string& path) {
    std::ifstream f(path);
    json j;
    f >> j;

    Plan p;
    p.abi_version = j.value("abi_version", 1u);
    p.sample_rate = j.value("sample_rate", 48000u);
    p.block_size = j.value("block_size", 128u);
    p.buffer_size = j.value("buffer_size", 0u);
    p.duration_frames = j.value("duration_frames", 0u);
    p.task_id = j.value("task_id", "default");

    for (const auto& n : j.value("nodes", json::array())) {
        p.nodes.push_back(n.get<std::string>());
    }
    for (const auto& n : j.value("execution_order", json::array())) {
        p.execution_order.push_back(n.get<std::string>());
    }

    auto node_configs = j.value("node_configs", json::object());
    for (auto it = node_configs.begin(); it != node_configs.end(); ++it) {
        NodeConfig cfg;
        cfg.id = it.key();
        cfg.component = it.value().value("component", "");
        cfg.version = it.value().value("version", "");
        cfg.task = it.value().value("task", "default");
        json params_json = it.value().value("params", json::object());
        for (auto pit = params_json.begin(); pit != params_json.end(); ++pit) {
            if (pit.value().is_string()) {
                cfg.params[pit.key()] = pit.value().get<std::string>();
            } else {
                cfg.params[pit.key()] = pit.value().dump();
            }
        }
        for (const auto& pid : it.value().value("input_ports", json::array())) {
            cfg.input_ports.push_back(pid.get<std::string>());
        }
        for (const auto& pid : it.value().value("output_ports", json::array())) {
            cfg.output_ports.push_back(pid.get<std::string>());
        }
        cfg.divisor = it.value().value("divisor", 1u);
        cfg.frames = it.value().value("frames", 0u);
        p.node_configs[it.key()] = cfg;
    }

    auto buffers = j.value("buffers", json::object());
    for (auto it = buffers.begin(); it != buffers.end(); ++it) {
        BufferConfig bc;
        bc.id = it.key();
        bc.from = it.value().value("from", "");
        bc.to = it.value().value("to", "");
        bc.sample_format = it.value().value("sample_format", "f32");
        bc.channels = it.value().value("channels", 2u);
        bc.frame_count = it.value().value("frame_count", p.block_size);
        p.buffers[it.key()] = bc;
    }

    for (const auto& c : j.value("connections", json::array())) {
        ConnectionConfig cc;
        cc.from = c.value("from", "");
        cc.to = c.value("to", "");
        cc.buffer = c.value("buffer", "");
        p.connections.push_back(cc);
    }

    for (const auto& m : j.value("modules", json::array())) {
        ModuleConfig mc;
        mc.path = m.value("path", "");
        mc.id = m.value("id", 0u);
        for (const auto& leaf : m.value("leaves", json::array())) {
            mc.leaves.emplace_back(leaf.value("node", ""), leaf.value("slot", 0u));
        }
        p.modules.push_back(mc);
    }

    for (const auto& e : j.value("id_map", json::array())) {
        IdMapEntry ie;
        ie.id = e.value("id", 0u);
        ie.node = e.value("node", "");
        ie.key = e.value("key", "");
        ie.kind = e.value("kind", "TUNE") == "RTC" ? ORPHEUS_ID_RTC
                 : e.value("kind", "TUNE") == "PROBE" ? ORPHEUS_ID_PROBE
                 : e.value("kind", "TUNE") == "STATE" ? ORPHEUS_ID_STATE
                 : e.value("kind", "TUNE") == "CUSTOM" ? ORPHEUS_ID_CUSTOM
                 : ORPHEUS_ID_TUNE;
        ie.form = e.value("form", "SCALAR") == "BULK" ? ORPHEUS_FORM_BULK
                  : e.value("form", "SCALAR") == "MODULE" ? ORPHEUS_FORM_MODULE
                  : ORPHEUS_FORM_SCALAR;
        ie.type = e.value("type", "float");
        ie.count = e.value("count", 1u);
        ie.name = e.value("name", ie.key);
        ie.runtime = e.value("runtime", false);
        ie.double_bank = e.value("double_bank", false);
        p.id_map.push_back(ie);
    }

    return p;
}

} // namespace orpheus
