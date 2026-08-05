#include "orpheus_runtime/plan.h"

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

    return p;
}

} // namespace orpheus
