#include "orpheus_runtime/runtime.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <stdlib.h>
#endif

namespace orpheus {

static std::string to_absolute_path(const std::string& path) {
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD len = GetFullPathNameA(path.c_str(), MAX_PATH, buffer, nullptr);
    if (len > 0 && len < MAX_PATH) {
        return std::string(buffer);
    }
#else
    char buffer[PATH_MAX];
    if (realpath(path.c_str(), buffer) != nullptr) {
        return std::string(buffer);
    }
#endif
    return path;
}

struct PortRef {
    std::string node_id;
    std::string port_id;
    explicit PortRef(const std::string& s) {
        size_t pos = s.find(':');
        if (pos == std::string::npos) {
            node_id = s;
            port_id = "";
        } else {
            node_id = s.substr(0, pos);
            port_id = s.substr(pos + 1);
        }
    }
};

Runtime::Runtime() {}

Runtime::~Runtime() {
    // Destroy instances in reverse order
    for (auto it = instances_.rbegin(); it != instances_.rend(); ++it) {
        Instance& inst = *it->second;
        if (inst.interface_ && inst.interface_->destroy) {
            inst.interface_->destroy(inst.state);
        }
    }
}

int Runtime::load_plan(const Plan& plan, const std::string& component_dir) {
    plan_ = plan;
    std::string abs_component_dir = to_absolute_path(component_dir);

    // Create component interfaces (one per component type)
    for (const auto& node_id : plan_.nodes) {
        const auto& cfg = plan_.node_configs[node_id];
        if (interfaces_.find(cfg.component) != interfaces_.end()) {
            continue;
        }

        // Map component id to library file name: orpheus.builtin.gain -> liborpheus_builtin_gain.dll
        std::string lib_name = cfg.component;
        for (char& c : lib_name) {
            if (c == '.') c = '_';
        }

#ifdef _WIN32
        std::string lib_path = abs_component_dir + "\\lib" + lib_name + ".dll";
#else
        std::string lib_path = abs_component_dir + "/lib" + lib_name + ".so";
#endif

        const OrpheusComponentInterface* iface = loader_.load(lib_path);
        if (!iface) {
            std::cerr << "[Runtime] failed to load " << lib_path << std::endl;
            return -1;
        }
        interfaces_[cfg.component] = iface;
    }

    // Allocate buffers
    for (const auto& kv : plan_.buffers) {
        const BufferConfig& bc = kv.second;
        uint32_t sample_count = bc.frame_count * bc.channels;
        auto buf = std::unique_ptr<OrpheusBuffer>(new OrpheusBuffer());
        buf->data = nullptr; // will point into buffer_memory_
        buf->format = ORPHEUS_FORMAT_F32;
        buf->channels = bc.channels;
        buf->frame_capacity = bc.frame_count;
        buf->frame_count = bc.frame_count;
        buf->interleaved = true;
        buffers_[bc.id] = std::move(buf);
    }

    // Reserve contiguous memory for all signal buffers
    size_t total_floats = 0;
    for (const auto& kv : buffers_) {
        total_floats += kv.second->frame_capacity * kv.second->channels;
    }
    buffer_memory_.resize(total_floats, 0.0f);

    size_t offset = 0;
    for (const auto& kv : buffers_) {
        OrpheusBuffer* buf = kv.second.get();
        buf->data = &buffer_memory_[offset];
        offset += buf->frame_capacity * buf->channels;
    }

    // Create instances
    for (const auto& node_id : plan_.execution_order) {
        const auto& cfg = plan_.node_configs[node_id];
        auto iface_it = interfaces_.find(cfg.component);
        if (iface_it == interfaces_.end()) {
            return -1;
        }
        const OrpheusComponentInterface* iface = iface_it->second;

        auto inst = std::unique_ptr<Instance>(new Instance());
        inst->node_id = node_id;
        inst->interface_ = iface;
        inst->state = nullptr;

        // Prepare config
        OrpheusConfig config;
        config.sample_rate = plan_.sample_rate;
        config.block_size = plan_.block_size;
        config.channels = 2; // default, will be overridden by param if needed
        config.param_count = 0;
        config.param_ids = nullptr;
        config.param_values = nullptr;

        int result = iface->create(&inst->state, &config);
        if (result != ORPHEUS_OK) {
            std::cerr << "[Runtime] create failed for " << node_id << std::endl;
            return result;
        }

        result = prepare_instance(*inst, cfg);
        if (result != ORPHEUS_OK) {
            return result;
        }

        instances_[node_id] = std::move(inst);
    }

    // Bind buffers to instance inputs/outputs based on connections
    for (const auto& conn : plan_.connections) {
        PortRef from_ref(conn.from);
        PortRef to_ref(conn.to);
        OrpheusBuffer* buf = buffers_[conn.buffer].get();

        Instance* from_inst = instances_[from_ref.node_id].get();
        Instance* to_inst = instances_[to_ref.node_id].get();

        // Simple binding: first output gets buf, first input gets buf
        // In full version, map by port_id
        if (from_ref.port_id == "out" || from_ref.port_id == "output") {
            from_inst->outputs.push_back(buf);
        } else {
            // For single-output components, treat any output port as first
            from_inst->outputs.push_back(buf);
        }

        if (to_ref.port_id == "in" || to_ref.port_id == "input") {
            to_inst->inputs.push_back(buf);
        } else {
            to_inst->inputs.push_back(buf);
        }
    }

    return 0;
}

int Runtime::prepare_instance(Instance& inst, const NodeConfig& cfg) {
    // Build parameter arrays
    std::vector<OrpheusValue> param_values;
    std::vector<std::string> param_id_storage;

    uint32_t channels = 2;
    for (const auto& kv : cfg.params) {
        param_id_storage.push_back(kv.first);
        OrpheusValue v;
        if (kv.first == "channels" || kv.first == "sample_rate") {
            v.type = ORPHEUS_VALUE_INT;
            v.value.i32 = static_cast<int32_t>(std::atoi(kv.second.c_str()));
            if (kv.first == "channels") {
                channels = static_cast<uint32_t>(v.value.i32);
            }
        } else if (kv.first == "file_path") {
            v.type = ORPHEUS_VALUE_STRING;
            v.value.str = kv.second.c_str();
        } else {
            v.type = ORPHEUS_VALUE_FLOAT;
            v.value.f32 = static_cast<float>(std::atof(kv.second.c_str()));
        }
        param_values.push_back(v);
    }

    std::vector<const char*> param_ids;
    for (const auto& s : param_id_storage) {
        param_ids.push_back(s.c_str());
    }

    OrpheusConfig config;
    config.sample_rate = plan_.sample_rate;
    config.block_size = plan_.block_size;
    config.channels = channels;
    config.param_ids = param_ids.empty() ? nullptr : param_ids.data();
    config.param_values = param_values.empty() ? nullptr : param_values.data();
    config.param_count = static_cast<uint32_t>(param_values.size());

    return inst.interface_->prepare(inst.state, &config);
}

int Runtime::set_parameter(const std::string& node_id, const std::string& param_id, const OrpheusValue& value) {
    auto it = instances_.find(node_id);
    if (it == instances_.end()) return -1;
    Instance& inst = *it->second;
    if (!inst.interface_->set_parameter) return -1;
    return inst.interface_->set_parameter(inst.state, param_id.c_str(), &value);
}

int Runtime::process_block(uint32_t frame_count) {
    OrpheusProcessContext ctx;
    ctx.frame_count = frame_count;
    ctx.scratch = nullptr;
    ctx.scratch_size = 0;
    ctx.timestamp = 0.0;

    for (const auto& node_id : plan_.execution_order) {
        Instance& inst = *instances_[node_id];
        ctx.state = inst.state;
        ctx.inputs = inst.inputs.empty() ? nullptr : const_cast<const OrpheusBuffer**>(inst.inputs.data());
        ctx.outputs = inst.outputs.empty() ? nullptr : inst.outputs.data();
        ctx.input_count = static_cast<uint32_t>(inst.inputs.size());
        ctx.output_count = static_cast<uint32_t>(inst.outputs.size());
        ctx.sample_rate = plan_.sample_rate;

        int result = inst.interface_->process(inst.state, &ctx);
        if (result != ORPHEUS_OK) {
            return result;
        }
    }
    return 0;
}

OrpheusBuffer* Runtime::get_input_buffer(const std::string& node_id, const std::string& port_id) {
    (void)port_id;
    auto it = instances_.find(node_id);
    if (it == instances_.end() || it->second->inputs.empty()) return nullptr;
    return it->second->inputs[0];
}

OrpheusBuffer* Runtime::get_output_buffer(const std::string& node_id, const std::string& port_id) {
    (void)port_id;
    auto it = instances_.find(node_id);
    if (it == instances_.end() || it->second->outputs.empty()) return nullptr;
    return it->second->outputs[0];
}

} // namespace orpheus
