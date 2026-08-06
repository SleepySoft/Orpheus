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

static size_t align_up(size_t v, size_t a) {
    if (a == 0) return v;
    return (v + a - 1) & ~(a - 1);
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
    // v2 统一内存拼接：先按每个实例的 state_size 计算总大小，再连续切片下发。
    size_t arena_total = 0;
    for (const auto& node_id : plan_.execution_order) {
        const auto& cfg = plan_.node_configs[node_id];
        auto iface_it = interfaces_.find(cfg.component);
        if (iface_it == interfaces_.end()) return -1;
        const OrpheusComponentDescriptor* desc = iface_it->second->get_descriptor();
        size_t align = desc->alignment > 0 ? (size_t)desc->alignment : 8;
        arena_total += align_up((size_t)desc->state_size, align);
    }
    state_arena_.assign(arena_total, 0);
    size_t arena_offset = 0;

    for (const auto& node_id : plan_.execution_order) {
        const auto& cfg = plan_.node_configs[node_id];
        auto iface_it = interfaces_.find(cfg.component);
        if (iface_it == interfaces_.end()) {
            return -1;
        }
        const OrpheusComponentInterface* iface = iface_it->second;
        const OrpheusComponentDescriptor* desc = iface->get_descriptor();

        auto inst = std::unique_ptr<Instance>(new Instance());
        inst->node_id = node_id;
        inst->interface_ = iface;
        inst->state = nullptr;
        inst->state_size = desc->state_size;

        size_t align = desc->alignment > 0 ? (size_t)desc->alignment : 8;
        size_t slice_size = align_up((size_t)desc->state_size, align);
        void* state_block = (arena_total > 0 && arena_offset < arena_total)
                                ? static_cast<void*>(state_arena_.data() + arena_offset)
                                : nullptr;
        arena_offset += slice_size;

        // Prepare config
        OrpheusConfig config;
        config.sample_rate = plan_.sample_rate;
        config.block_size = plan_.block_size;
        config.channels = 2; // default, will be overridden by param if needed
        config.param_count = 0;
        config.param_ids = nullptr;
        config.param_values = nullptr;
        config.state_block = state_block;

        int result = iface->create(&inst->state, &config);
        if (result != ORPHEUS_OK) {
            std::cerr << "[Runtime] create failed for " << node_id << std::endl;
            return result;
        }

        // v2 主动注册：组件把槽（地址/类型/说明）注册进实例槽表。
        // 旧 DLL（abi_version=1）或未实现 register_slots 的组件走 v1 回调路径。
        if (desc->abi_version >= 2 && iface->register_slots) {
            OrpheusRegistry reg;
            reg.ctx = inst.get();
            reg.add = [](void* ctx, const OrpheusSlotInfo* info) -> OrpheusSlotId {
                Instance* inst = static_cast<Instance*>(ctx);
                if (info == nullptr || info->key == nullptr || info->count == 0) {
                    return ORPHEUS_SLOT_ID_INVALID;
                }
                /* 上下边界：整块越界与内部越界在此拒绝 */
                size_t span = (size_t)info->count * info->size;
                if (info->offset + span > inst->state_size) {
                    return ORPHEUS_SLOT_ID_INVALID;
                }
                if (inst->slot_index.find(info->key) != inst->slot_index.end()) {
                    return ORPHEUS_SLOT_ID_INVALID;  /* 键重复 */
                }
                SlotEntry e;
                e.key = info->key;
                e.kind = info->kind;
                e.type = info->type;
                e.offset = info->offset;
                e.size = info->size;
                e.count = info->count;
                e.min_f32 = info->min_f32;
                e.max_f32 = info->max_f32;
                e.min_i32 = info->min_i32;
                e.max_i32 = info->max_i32;
                e.unit = info->unit ? info->unit : "";
                e.update_policy = info->update_policy;
                e.flags = info->flags;
                size_t idx = inst->slots.size();
                inst->slots.push_back(std::move(e));
                inst->slot_index[info->key] = idx;
                return static_cast<OrpheusSlotId>(idx);
            };
            reg.update = nullptr;
            iface->register_slots(inst->state, &reg);
        }

        result = prepare_instance(*inst, cfg);
        if (result != ORPHEUS_OK) {
            std::cerr << "[Runtime] prepare failed for node " << node_id
                      << " (" << cfg.component << "): " << result << std::endl;
            return result;
        }

        instances_[node_id] = std::move(inst);
    }

    // Size port arrays and build port id -> slot index maps
    for (auto& kv : instances_) {
        Instance& inst = *kv.second;
        const NodeConfig& cfg = plan_.node_configs[inst.node_id];
        for (size_t i = 0; i < cfg.input_ports.size(); ++i) {
            inst.input_index[cfg.input_ports[i]] = i;
        }
        for (size_t i = 0; i < cfg.output_ports.size(); ++i) {
            inst.output_index[cfg.output_ports[i]] = i;
        }
        inst.inputs.resize(cfg.input_ports.size(), nullptr);
        inst.outputs.resize(cfg.output_ports.size(), nullptr);
    }

    // Bind buffers to instance inputs/outputs by port id (unconnected pins stay nullptr).
    // Fan-out: connections sharing the same source port share one buffer, so every
    // downstream node reads the data written by the producer (consumers are read-only).
    std::map<std::string, OrpheusBuffer*> fanout_buffer;
    for (const auto& conn : plan_.connections) {
        PortRef from_ref(conn.from);
        PortRef to_ref(conn.to);
        OrpheusBuffer* buf = buffers_[conn.buffer].get();
        auto shared = fanout_buffer.find(conn.from);
        if (shared != fanout_buffer.end()) {
            buf = shared->second;
        } else {
            fanout_buffer[conn.from] = buf;
        }

        Instance* from_inst = instances_[from_ref.node_id].get();
        Instance* to_inst = instances_[to_ref.node_id].get();
        if (!from_inst || !to_inst) {
            std::cerr << "[Runtime] connection references unknown node: "
                      << conn.from << " -> " << conn.to << std::endl;
            return -1;
        }

        auto oi = from_inst->output_index.find(from_ref.port_id);
        if (oi != from_inst->output_index.end()) {
            from_inst->outputs[oi->second] = buf;
        } else if (from_inst->output_index.empty()) {
            from_inst->outputs.push_back(buf);  // legacy plans without port lists
        } else {
            std::cerr << "[Runtime] unknown output port: " << conn.from << std::endl;
            return -1;
        }

        auto ii = to_inst->input_index.find(to_ref.port_id);
        if (ii != to_inst->input_index.end()) {
            to_inst->inputs[ii->second] = buf;
        } else if (to_inst->input_index.empty()) {
            to_inst->inputs.push_back(buf);  // legacy plans without port lists
        } else {
            std::cerr << "[Runtime] unknown input port: " << conn.to << std::endl;
            return -1;
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
        } else {
            // numeric strings become FLOAT, anything else (e.g. "lowpass") stays STRING
            const std::string& s = kv.second;
            char* end = nullptr;
            float f = std::strtof(s.c_str(), &end);
            if (end != s.c_str() && end && *end == '\0') {
                v.type = ORPHEUS_VALUE_FLOAT;
                v.value.f32 = f;
            } else {
                v.type = ORPHEUS_VALUE_STRING;
                v.value.str = s.c_str();
            }
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
    config.state_block = inst.state;
    config.param_ids = param_ids.empty() ? nullptr : param_ids.data();
    config.param_values = param_values.empty() ? nullptr : param_values.data();
    config.param_count = static_cast<uint32_t>(param_values.size());

    return inst.interface_->prepare(inst.state, &config);
}

int Runtime::set_parameter(const std::string& node_id, const std::string& param_id, const OrpheusValue& value) {
    auto it = instances_.find(node_id);
    if (it == instances_.end()) return -1;
    Instance& inst = *it->second;
    // v2 槽直写：SETTING 标量且非 restart_required 时，按注册地址类型化写入。
    auto si = inst.slot_index.find(param_id);
    if (si != inst.slot_index.end()) {
        const SlotEntry& e = inst.slots[si->second];
        if (e.kind == ORPHEUS_SLOT_SETTING && e.count == 1 && inst.state != nullptr &&
            e.update_policy != ORPHEUS_UPDATE_RESTART_REQUIRED &&
            (e.flags & ORPHEUS_SLOT_DIRECT_WRITE) != 0 &&
            value.type == e.type && e.offset + e.size <= inst.state_size) {
            char* p = static_cast<char*>(inst.state) + e.offset;
            if (e.type == ORPHEUS_VALUE_FLOAT && e.size >= sizeof(float)) {
                std::memcpy(p, &value.value.f32, sizeof(float));
                return ORPHEUS_OK;
            }
            if (e.type == ORPHEUS_VALUE_INT && e.size >= sizeof(int32_t)) {
                std::memcpy(p, &value.value.i32, sizeof(int32_t));
                return ORPHEUS_OK;
            }
            if (e.type == ORPHEUS_VALUE_BOOL && e.size >= 1) {
                *p = value.value.b ? 1 : 0;
                return ORPHEUS_OK;
            }
        }
    }
    if (!inst.interface_->set_parameter) return -1;
    return inst.interface_->set_parameter(inst.state, param_id.c_str(), &value);
}

int Runtime::get_parameter(const std::string& node_id, const std::string& param_id, OrpheusValue* value) {
    auto it = instances_.find(node_id);
    if (it == instances_.end()) return -1;
    Instance& inst = *it->second;
    // v2 槽直读：PROBE 槽始终直读；SETTING 槽仅 DIRECT_WRITE（存储值即语义值）时直读，
    // 其余回退回调——保留派生读回语义（如 bass gain_db 读的是平滑后的当前值）。
    auto si = inst.slot_index.find(param_id);
    if (si != inst.slot_index.end()) {
        const SlotEntry& e = inst.slots[si->second];
        if (e.count == 1 && inst.state != nullptr &&
            (e.kind == ORPHEUS_SLOT_PROBE ||
             (e.kind == ORPHEUS_SLOT_SETTING && (e.flags & ORPHEUS_SLOT_DIRECT_WRITE) != 0)) &&
            e.offset + e.size <= inst.state_size) {
            const char* p = static_cast<const char*>(inst.state) + e.offset;
            if (e.type == ORPHEUS_VALUE_FLOAT && e.size >= sizeof(float)) {
                value->type = ORPHEUS_VALUE_FLOAT;
                std::memcpy(&value->value.f32, p, sizeof(float));
                return ORPHEUS_OK;
            }
            if (e.type == ORPHEUS_VALUE_INT && e.size >= sizeof(int32_t)) {
                value->type = ORPHEUS_VALUE_INT;
                std::memcpy(&value->value.i32, p, sizeof(int32_t));
                return ORPHEUS_OK;
            }
            if (e.type == ORPHEUS_VALUE_BOOL && e.size >= 1) {
                value->type = ORPHEUS_VALUE_BOOL;
                value->value.b = *p != 0;
                return ORPHEUS_OK;
            }
        }
    }
    if (!inst.interface_->get_parameter) return -1;
    return inst.interface_->get_parameter(inst.state, param_id.c_str(), value);
}

int Runtime::write_bulk(const std::string& node_id, const std::string& key,
                        const void* data, size_t count) {
    auto it = instances_.find(node_id);
    if (it == instances_.end()) return -1;
    Instance& inst = *it->second;
    auto si = inst.slot_index.find(key);
    if (si == inst.slot_index.end()) return -1;
    const SlotEntry& e = inst.slots[si->second];
    if (e.kind != ORPHEUS_SLOT_BULK) return -1;
    if (count > e.count) return -1;                  /* 内部边界：不超过槽容量 */
    size_t span = count * e.size;
    if (inst.state == nullptr || e.offset + span > inst.state_size) return -1; /* 上下边界 */
    std::memcpy(static_cast<char*>(inst.state) + e.offset, data, span);
    return ORPHEUS_OK;
}

const OrpheusComponentInterface* Runtime::get_interface(const std::string& node_id) {
    auto it = instances_.find(node_id);
    if (it == instances_.end()) return nullptr;
    return it->second->interface_;
}

int Runtime::process_block(uint32_t frame_count) {
    OrpheusProcessContext ctx;
    ctx.scratch = nullptr;
    ctx.scratch_size = 0;
    ctx.timestamp = 0.0;

    for (const auto& node_id : plan_.execution_order) {
        const NodeConfig& cfg = plan_.node_configs[node_id];
        // multi-rate scheduling: fire only on the node's rate phase
        if (cfg.divisor > 1 && (block_counter_ + 1) % cfg.divisor != 0) {
            continue;
        }
        Instance& inst = *instances_[node_id];
        ctx.state = inst.state;
        ctx.frame_count = cfg.frames > 0 ? cfg.frames : frame_count;
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
    block_counter_++;
    return 0;
}

OrpheusBuffer* Runtime::get_input_buffer(const std::string& node_id, const std::string& port_id) {
    auto it = instances_.find(node_id);
    if (it == instances_.end()) return nullptr;
    Instance& inst = *it->second;
    auto idx = inst.input_index.find(port_id);
    if (idx != inst.input_index.end()) return inst.inputs[idx->second];
    return inst.inputs.empty() ? nullptr : inst.inputs[0];
}

OrpheusBuffer* Runtime::get_output_buffer(const std::string& node_id, const std::string& port_id) {
    auto it = instances_.find(node_id);
    if (it == instances_.end()) return nullptr;
    Instance& inst = *it->second;
    auto idx = inst.output_index.find(port_id);
    if (idx != inst.output_index.end()) return inst.outputs[idx->second];
    return inst.outputs.empty() ? nullptr : inst.outputs[0];
}

} // namespace orpheus
