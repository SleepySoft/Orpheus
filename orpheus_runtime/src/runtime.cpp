#include "orpheus_runtime/runtime.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <functional>
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
#elif defined(__APPLE__)
    std::string lib_path = abs_component_dir + "/lib" + lib_name + ".dylib";
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
    // v2 模块连续内存：按 plan.modules 递归布局，每个模块（含根）一块连续内存，
    // 叶子状态按执行序排在模块内、子模块紧随其后——与生成路径的嵌套结构体同一规则。
    const size_t kAlign = 8;
    std::map<std::string, std::vector<std::string>> module_children;
    std::map<std::string, uint32_t> module_id_of;
    for (const auto& m : plan_.modules) {
        module_id_of[m.path] = m.id;
        if (m.path.empty()) continue;
        size_t sep = m.path.rfind("__");
        std::string parent = sep == std::string::npos ? "" : m.path.substr(0, sep);
        module_children[parent].push_back(m.path);
    }
    std::map<std::string, std::pair<size_t, size_t>> node_size_align;
    for (const auto& node_id : plan_.execution_order) {
        auto iface_it = interfaces_.find(plan_.node_configs[node_id].component);
        if (iface_it == interfaces_.end()) return -1;
        const OrpheusComponentDescriptor* desc = iface_it->second->get_descriptor();
        node_size_align[node_id] = {
            (size_t)desc->state_size,
            desc->alignment > 0 ? (size_t)desc->alignment : kAlign,
        };
    }
    std::map<std::string, size_t> module_size;
    std::function<size_t(const std::string&)> compute_size =
        [&](const std::string& path) -> size_t {
        size_t size = 0;
        for (const auto& m : plan_.modules) {
            if (m.path != path) continue;
            for (const auto& leaf : m.leaves) {
                auto it = node_size_align.find(leaf.first);
                if (it == node_size_align.end() || it->second.first == 0) continue;
                size += align_up(it->second.first, it->second.second);
            }
        }
        for (const auto& child : module_children[path]) {
            size += align_up(compute_size(child), kAlign);
        }
        module_size[path] = size;
        return size;
    };
    size_t arena_total = compute_size("");
    state_arena_.assign(arena_total, 0);

    std::map<std::string, size_t> node_offset;
    std::function<void(const std::string&, size_t)> assign_layout =
        [&](const std::string& path, size_t base) {
        size_t cursor = base;
        for (const auto& m : plan_.modules) {
            if (m.path != path) continue;
            for (const auto& leaf : m.leaves) {
                auto it = node_size_align.find(leaf.first);
                if (it == node_size_align.end() || it->second.first == 0) continue;
                cursor = align_up(cursor, it->second.second);
                node_offset[leaf.first] = cursor;
                cursor += align_up(it->second.first, it->second.second);
            }
        }
        for (const auto& child : module_children[path]) {
            size_t cb = align_up(cursor, kAlign);
            assign_layout(child, cb);
            module_layout_[module_id_of[child]] = {cb, module_size[child]};
            cursor = cb + module_size[child];
        }
        if (path.empty()) {
            module_layout_[0] = {0, arena_total};  // 根模块
        }
    };
    assign_layout("", 0);

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

        auto no_it = node_offset.find(node_id);
        void* state_block = (no_it != node_offset.end())
                                ? static_cast<void*>(state_arena_.data() + no_it->second)
                                : nullptr;

        // Prepare config
        OrpheusConfig config;
        config.sample_rate = cfg.sample_rate > 0 ? cfg.sample_rate : plan_.sample_rate;
        config.block_size = cfg.block_size > 0 ? cfg.block_size : (cfg.frames > 0 ? cfg.frames : plan_.block_size);
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
                e.name = info->name ? info->name : "";
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
    // rate-bridge 边（merge 节点输入）：生产者不直写桥接 buffer，而是写共享 staging
    // （自己的块长），触发后由 process_block 按写游标滚入桥接 buffer（深度=合流量子）。
    std::map<std::string, OrpheusBuffer*> fanout_buffer;
    for (const auto& conn : plan_.connections) {
        PortRef from_ref(conn.from);
        PortRef to_ref(conn.to);
        OrpheusBuffer* edge_buf = buffers_[conn.buffer].get();
        const BufferConfig& buffer_cfg = plan_.buffers[conn.buffer];
        const bool is_bridge = buffer_cfg.rate_bridge || buffer_cfg.task_bridge;

        Instance* from_inst = instances_[from_ref.node_id].get();
        Instance* to_inst = instances_[to_ref.node_id].get();
        if (!from_inst || !to_inst) {
            std::cerr << "[Runtime] connection references unknown node: "
                      << conn.from << " -> " << conn.to << std::endl;
            return -1;
        }

        // 源端口直写 buffer：普通边=共享 fanout 边 buffer；桥接源=独立 staging。
        OrpheusBuffer* src_buf = nullptr;
        auto shared = fanout_buffer.find(conn.from);
        if (shared != fanout_buffer.end()) {
            src_buf = shared->second;
        } else if (!is_bridge) {
            src_buf = edge_buf;
            fanout_buffer[conn.from] = src_buf;
        } else {
            const NodeConfig& from_cfg = plan_.node_configs[from_ref.node_id];
            uint32_t stride = 0;
            auto bs_it = from_cfg.output_port_block_sizes.find(from_ref.port_id);
            if (bs_it != from_cfg.output_port_block_sizes.end()) stride = bs_it->second;
            if (stride == 0) stride = from_cfg.block_size > 0 ? from_cfg.block_size : plan_.block_size;
            uint32_t chans = 1;
            auto ch_it = from_cfg.output_port_channels.find(from_ref.port_id);
            if (ch_it != from_cfg.output_port_channels.end()) chans = ch_it->second;
            auto staging = std::unique_ptr<OrpheusBuffer>(new OrpheusBuffer());
            staging->format = ORPHEUS_FORMAT_F32;
            staging->channels = chans;
            staging->frame_capacity = stride;
            staging->frame_count = stride;
            staging->interleaved = true;
            auto mem = std::unique_ptr<float[]>(new float[static_cast<size_t>(stride) * chans]());
            staging->data = mem.get();
            staging_memory_.push_back(std::move(mem));
            src_buf = staging.get();
            staging_buffers_.push_back(std::move(staging));
            fanout_buffer[conn.from] = src_buf;
        }

        auto oi = from_inst->output_index.find(from_ref.port_id);
        if (oi != from_inst->output_index.end()) {
            from_inst->outputs[oi->second] = src_buf;
        } else if (from_inst->output_index.empty()) {
            from_inst->outputs.push_back(src_buf);  // legacy plans without port lists
        } else {
            std::cerr << "[Runtime] unknown output port: " << conn.from << std::endl;
            return -1;
        }

        OrpheusBuffer* dst_buf = is_bridge ? edge_buf : src_buf;
        auto ii = to_inst->input_index.find(to_ref.port_id);
        if (ii != to_inst->input_index.end()) {
            to_inst->inputs[ii->second] = dst_buf;
        } else if (to_inst->input_index.empty()) {
            to_inst->inputs.push_back(dst_buf);  // legacy plans without port lists
        } else {
            std::cerr << "[Runtime] unknown input port: " << conn.to << std::endl;
            return -1;
        }

        if (buffer_cfg.rate_bridge) {
            BridgeCopy cp;
            cp.staging = src_buf;
            cp.bridge = edge_buf;
            cp.frames = src_buf->frame_capacity;
            cp.channels = src_buf->channels;
            bridge_copies_[from_ref.node_id].push_back(cp);
        }
        if (buffer_cfg.task_bridge) {
            auto bridge = std::unique_ptr<TaskBridge>(new TaskBridge());
            bridge->staging = src_buf;
            bridge->consumer = edge_buf;
            bridge->consumer_node = to_ref.node_id;
            bridge->channels = edge_buf->channels;
            bridge->legacy_rate_bridge = buffer_cfg.rate_bridge;
            bridge->capacity_frames = buffer_cfg.capacity_frames > 0
                                          ? buffer_cfg.capacity_frames
                                          : edge_buf->frame_capacity * 2;
            bridge->ring.reset(new float[static_cast<size_t>(bridge->capacity_frames)
                                         * bridge->channels]());
            TaskBridge* ptr = bridge.get();
            task_bridges_.push_back(std::move(bridge));
            task_bridge_writes_[from_ref.node_id].push_back(ptr);
            task_bridge_reads_[to_ref.node_id].push_back(ptr);
        }
    }

    // 为悬空输出端口分配丢弃缓冲区，使组件 process 永远看不到 NULL 输出。
    // 块长优先使用 compiler 解析后的端口 block_size；未记录则回退到节点输入块长。
    // v2 悬空输出丢弃缓冲区：先全局累计各端口大小并 reserve，
    // 避免后续 resize 移动 vector 时使先前已绑定的 data 指针变为野指针。
    {
        size_t total_discard = 0;
        for (auto& kv : instances_) {
            const NodeConfig& cfg = plan_.node_configs[kv.second->node_id];
            for (size_t i = 0; i < cfg.output_ports.size(); ++i) {
                if (kv.second->outputs[i] != nullptr) continue;
                size_t bs = cfg.block_size > 0 ? cfg.block_size
                          : (cfg.frames > 0 ? cfg.frames : plan_.block_size);
                const std::string& port_id = cfg.output_ports[i];
                auto bs_it = cfg.output_port_block_sizes.find(port_id);
                if (bs_it != cfg.output_port_block_sizes.end()) bs = bs_it->second;
                uint32_t chans = 1;
                const OrpheusComponentDescriptor* desc = kv.second->interface_->get_descriptor();
                auto ch_it = cfg.output_port_channels.find(port_id);
                if (ch_it != cfg.output_port_channels.end()) {
                    chans = ch_it->second;
                } else if (desc != nullptr) {
                    for (uint32_t pi = 0; pi < desc->port_count; ++pi) {
                        const OrpheusPort* p = &desc->ports[pi];
                        if (p->id == nullptr || port_id != p->id) continue;
                        if (p->channels > 0) chans = p->channels;
                        else if (p->is_variable && p->channels_param != nullptr) {
                            auto pit = cfg.params.find(p->channels_param);
                            if (pit != cfg.params.end()) {
                                try { chans = static_cast<uint32_t>(std::stoul(pit->second)); }
                                catch (...) { chans = 1; }
                            }
                        }
                        break;
                    }
                }
                total_discard += static_cast<size_t>(chans) * bs;
            }
        }
        discard_memory_.clear();
        discard_memory_.reserve(total_discard);   // 预分配，后续 resize 不再移动
    }

    {
        size_t cursor = 0;
        for (auto& kv : instances_) {
            Instance& inst = *kv.second;
            const NodeConfig& cfg = plan_.node_configs[inst.node_id];
            const OrpheusComponentDescriptor* desc = inst.interface_->get_descriptor();
            for (size_t i = 0; i < cfg.output_ports.size(); ++i) {
                if (inst.outputs[i] != nullptr) continue;
                const std::string& port_id = cfg.output_ports[i];
                uint32_t block_size = cfg.block_size > 0 ? cfg.block_size : (cfg.frames > 0 ? cfg.frames : plan_.block_size);
                auto bs_it = cfg.output_port_block_sizes.find(port_id);
                if (bs_it != cfg.output_port_block_sizes.end()) {
                    block_size = bs_it->second;
                }
                uint32_t channels = 1;
                auto ch_it = cfg.output_port_channels.find(port_id);
                if (ch_it != cfg.output_port_channels.end()) {
                    channels = ch_it->second;
                } else if (desc != nullptr) {
                    for (uint32_t pi = 0; pi < desc->port_count; ++pi) {
                        const OrpheusPort* p = &desc->ports[pi];
                        if (p->id == nullptr || port_id != p->id) continue;
                        if (p->channels > 0) {
                            channels = p->channels;
                        } else if (p->is_variable && p->channels_param != nullptr) {
                            auto pit = cfg.params.find(p->channels_param);
                            if (pit != cfg.params.end()) {
                                try {
                                    channels = static_cast<uint32_t>(std::stoul(pit->second));
                                } catch (...) {
                                    channels = 1;
                                }
                            }
                        }
                        break;
                    }
                }
                size_t n = static_cast<size_t>(channels) * block_size;
                auto discard = std::unique_ptr<OrpheusBuffer>(new OrpheusBuffer());
                discard->format = ORPHEUS_FORMAT_F32;
                discard->channels = channels;
                discard->frame_capacity = block_size;
                discard->frame_count = block_size;
                discard->interleaved = true;
                // 已 reserve，resize 仅在预分配包围内扩展，指针稳定
                discard_memory_.resize(cursor + n, 0.0f);
                discard->data = discard_memory_.data() + cursor;
                cursor += n;
                inst.outputs[i] = discard.get();
                discard_buffers_.push_back(std::move(discard));
            }
        }
    }

    // BULK 双 bank（可选）：仅对「生效」的槽分配影子区（工程 auto/on/off × 组件声明）
    std::map<std::string, bool> db_enabled;
    for (const auto& e : plan_.id_map) {
        db_enabled[e.node + "\x1f" + e.key] = e.double_bank;
    }
    size_t shadow_total = 0;
    for (auto& kv : instances_) {
        for (const auto& e : kv.second->slots) {
            if (e.kind == ORPHEUS_SLOT_BULK) {
                auto db = db_enabled.find(kv.first + "\x1f" + e.key);
                bool enabled = db != db_enabled.end()
                    ? db->second
                    : (e.flags & ORPHEUS_SLOT_DOUBLE_BUFFERED) != 0;
                if (enabled) shadow_total += align_up(e.count * e.size, 8);
            }
        }
    }
    bulk_shadow_.assign(shadow_total, 0);
    bulk_shadow_map_.clear();
    bulk_active_map_.clear();
    bulk_span_map_.clear();
    bulk_pending_.clear();
    size_t shadow_offset = 0;
    for (auto& kv : instances_) {
        for (const auto& e : kv.second->slots) {
            if (e.kind != ORPHEUS_SLOT_BULK) continue;
            std::string key = kv.first + "\x1f" + e.key;
            auto db = db_enabled.find(key);
            bool enabled = db != db_enabled.end()
                ? db->second
                : (e.flags & ORPHEUS_SLOT_DOUBLE_BUFFERED) != 0;
            if (!enabled) continue;  // 未开启双 bank：直写 active（部署省内存）
            bulk_shadow_map_[key] = bulk_shadow_.data() + shadow_offset;
            bulk_active_map_[key] = static_cast<uint8_t*>(kv.second->state) + e.offset;
            bulk_span_map_[key] = e.count * e.size;
            bulk_pending_[key] = false;
            shadow_offset += align_up(e.count * e.size, 8);
        }
    }

    // 数据 ID 索引：plan.id_map → 条目（指向 plan_ 内部存储，加载后不再变动）
    id_index_.clear();
    key_to_id_.clear();
    for (const auto& e : plan_.id_map) {
        id_index_[e.id] = &e;
        key_to_id_[e.node + "\x1f" + e.key] = e.id;
    }

    // 控制链路运行态初始化（快照/字符串缓冲在此预分配，process 路径零分配）。
    // 本期运行期仅执行 float/int/bool 标量（count==1）与 string 原样透传；
    // count>1 的数值数组链编译期已做形状校验，运行期跳过。
    control_links_.clear();
    size_t skipped_links = 0;
    for (const auto& cl : plan_.control_links) {
        ControlLinkState st;
        st.cfg = &cl;
        st.skip = (cl.type != "string") && cl.count > 1;
        if (cl.type == "string") {
            st.str_buf.assign(256, '\0');  // 预分配上限，超长截断
        }
        if (st.skip) ++skipped_links;
        control_links_.push_back(std::move(st));
    }
    if (skipped_links > 0) {
        std::cerr << "[Runtime] 控制链路：" << skipped_links
                  << " 条数组链（count>1）本期运行时不执行，仅编译期校验" << std::endl;
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
    config.sample_rate = cfg.sample_rate > 0 ? cfg.sample_rate : plan_.sample_rate;
    config.block_size = cfg.block_size > 0 ? cfg.block_size : (cfg.frames > 0 ? cfg.frames : plan_.block_size);
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
    /* 双 bank：写影子区，process_block 块边界提交到 active（process 读的是 active） */
    auto sm = bulk_shadow_map_.find(node_id + "\x1f" + key);
    if (sm == bulk_shadow_map_.end()) {
        /* 未开启双 bank：直写 active（部署时内存受限路径） */
        std::memcpy(static_cast<char*>(inst.state) + e.offset, data, span);
        return ORPHEUS_OK;
    }
    std::memcpy(sm->second, data, span);
    bulk_pending_[node_id + "\x1f" + key] = true;
    return ORPHEUS_OK;
}

int Runtime::get_bulk(const std::string& node_id, const std::string& key,
                      void* out, size_t count) {
    if (out == nullptr) return -1;
    auto it = instances_.find(node_id);
    if (it == instances_.end()) return -1;
    const Instance& inst = *it->second;
    auto si = inst.slot_index.find(key);
    if (si == inst.slot_index.end()) return -1;
    const SlotEntry& e = inst.slots[si->second];
    if (e.kind != ORPHEUS_SLOT_BULK) return -1;
    if (count > e.count) return -1;                  /* 内部边界 */
    size_t span = count * e.size;
    if (inst.state == nullptr || e.offset + span > inst.state_size) return -1; /* 上下边界 */
    std::memcpy(out, static_cast<const char*>(inst.state) + e.offset, span);   /* 仅拷贝 */
    return ORPHEUS_OK;
}

int Runtime::get_bulk_id(uint32_t id, void* out, size_t count) {
    OrpheusResolvedData d;
    if (resolve(id, &d) != ORPHEUS_OK) return ORPHEUS_ERR_NOT_FOUND;
    if (d.form != ORPHEUS_FORM_BULK) return ORPHEUS_ERR_INVALID_ARG;
    return get_bulk(d.node, d.key, out, count);
}

bool Runtime::lookup_id(const std::string& node_id, const std::string& key,
                        uint32_t* out_id) const {
    auto it = key_to_id_.find(node_id + "\x1f" + key);
    if (it == key_to_id_.end()) return false;
    if (out_id) *out_id = it->second;
    return true;
}

namespace {
constexpr size_t kMsgHdrSize = sizeof(OrpheusMessageHeader);

uint32_t msg_event_of(uint32_t kind, bool write) {
    switch (kind) {
        case ORPHEUS_ID_RTC: return write ? ORPHEUS_EVENT_RTC_WRITE : ORPHEUS_EVENT_RTC_READ;
        case ORPHEUS_ID_TUNE: return write ? ORPHEUS_EVENT_TUNE_WRITE : ORPHEUS_EVENT_TUNE_READ;
        case ORPHEUS_ID_PROBE: return ORPHEUS_EVENT_PROBE_READ;
        case ORPHEUS_ID_STATE: return ORPHEUS_EVENT_STATE_READ;
        default: return ORPHEUS_EVENT_CUSTOM;
    }
}

size_t msg_write_response(uint8_t* out, size_t out_cap, uint32_t route,
                          uint32_t call_id, uint32_t flags, uint32_t words) {
    if (out_cap < kMsgHdrSize + (size_t)words * 4) return 0;
    OrpheusMessageHeader* h = reinterpret_cast<OrpheusMessageHeader*>(out);
    h->route_id = route;
    h->bits = ORPHEUS_MSG_MAKE(ORPHEUS_MSG_RESPONSE, flags, call_id, words);
    return kMsgHdrSize + (size_t)words * 4;
}
}  // namespace

int Runtime::register_hook(uint32_t id, OrpheusHookFn fn, void* ctx) {
    if (fn == nullptr) return -1;
    hooks_[id] = {fn, ctx};
    return 0;
}

int Runtime::message(const uint8_t* in, size_t in_len, uint8_t* out,
                     size_t out_cap, size_t* out_len) {
    if (in == nullptr || in_len < kMsgHdrSize || out == nullptr || out_len == nullptr) return -1;
    *out_len = 0;
    const OrpheusMessageHeader* hdr = reinterpret_cast<const OrpheusMessageHeader*>(in);
    size_t words = ORPHEUS_MSG_PAYLOAD_WORDS(hdr);
    if (kMsgHdrSize + words * 4 > in_len) return -1;
    uint32_t route = hdr->route_id;
    uint32_t call_id = ORPHEUS_MSG_CALL_ID(hdr);
    OrpheusBlob req{in + kMsgHdrSize, static_cast<uint32_t>(words * 4)};
    uint32_t msg_type = ORPHEUS_MSG_TYPE(hdr);

    if (msg_type == ORPHEUS_MSG_NOTIFICATION) {
        /* 单向分发：外部 hook 或组件 hook；resp=NULL（notification 无返回） */
        auto h = hooks_.find(route);
        if (h != hooks_.end()) {
            h->second.fn(h->second.ctx, route, ORPHEUS_EVENT_CUSTOM, &req, nullptr);
            return 0;
        }
        OrpheusResolvedData d;
        if (resolve(route, &d) == ORPHEUS_OK && d.node && d.key && d.form != ORPHEUS_FORM_MODULE) {
            auto ii = instances_.find(d.node);
            if (ii != instances_.end() &&
                ii->second->interface_->get_descriptor()->abi_version >= 3 &&
                ii->second->interface_->hook) {
                ii->second->interface_->hook(ii->second->state, route,
                                             msg_event_of(d.kind, false), &req, nullptr);
            }
        }
        return 0;
    }
    if (msg_type != ORPHEUS_MSG_CALL) return -1;

    /* CALL → 同步 RESPONSE（回显 call_id；错误置 flags 错误位） */
    OrpheusBlob resp{out + kMsgHdrSize, 0};
    auto hook_resp = [&](OrpheusHookFn fn, void* ctx, uint32_t event) -> size_t {
        int r = fn(ctx, route, event, &req, &resp);
        if (r == ORPHEUS_HOOK_ERROR || resp.len > out_cap - kMsgHdrSize) {
            return msg_write_response(out, out_cap, route, call_id, ORPHEUS_MSG_FLAG_ERROR, 0);
        }
        if (r == ORPHEUS_HOOK_HANDLED) {
            return msg_write_response(out, out_cap, route, call_id, 0, (resp.len + 3) / 4);
        }
        return 0;  /* CONTINUE */
    };

    size_t len = 0;
    auto h = hooks_.find(route);
    if (h != hooks_.end()) {
        OrpheusResolvedData d0;
        uint32_t event = ORPHEUS_EVENT_CUSTOM;
        if (resolve(route, &d0) == ORPHEUS_OK) event = msg_event_of(d0.kind, words > 0);
        len = hook_resp(h->second.fn, h->second.ctx, event);
        if (len > 0) { *out_len = len; return 0; }
    }
    {
        OrpheusResolvedData d;
        if (resolve(route, &d) == ORPHEUS_OK && d.node && d.key && d.form != ORPHEUS_FORM_MODULE) {
            auto ii = instances_.find(d.node);
            if (ii != instances_.end() &&
                ii->second->interface_->get_descriptor()->abi_version >= 3 &&
                ii->second->interface_->hook) {
                len = hook_resp(ii->second->interface_->hook, ii->second->state,
                                msg_event_of(d.kind, words > 0));
                if (len > 0) { *out_len = len; return 0; }
            }
        }
    }
    /* 默认语义 */
    uint32_t resp_words = 0, resp_flags = 0;
    int rc = msg_default(route, req, words > 0, out, out_cap, &resp_words, &resp_flags);
    if (rc != 0) return rc;
    len = msg_write_response(out, out_cap, route, call_id, resp_flags, resp_words);
    if (len == 0) return -1;
    *out_len = len;
    return 0;
}

int Runtime::msg_default(uint32_t route, const OrpheusBlob& req, bool write,
                         uint8_t* out, size_t out_cap, uint32_t* resp_words, uint32_t* resp_flags) {
    OrpheusResolvedData d;
    if (resolve(route, &d) != ORPHEUS_OK) { *resp_flags = ORPHEUS_MSG_FLAG_ERROR; return 0; }
    if (d.kind == ORPHEUS_ID_CUSTOM || d.form == ORPHEUS_FORM_MODULE) {
        *resp_flags = ORPHEUS_MSG_FLAG_ERROR;  /* CUSTOM 必须由 hook 处理；模块包整块暂不支持 */
        return 0;
    }
    if ((d.kind == ORPHEUS_ID_PROBE || d.kind == ORPHEUS_ID_STATE) && write) {
        *resp_flags = ORPHEUS_MSG_FLAG_ERROR;  /* 只读 */
        return 0;
    }
    if (write) {
        if (d.form == ORPHEUS_FORM_BULK) {
            size_t n = req.len / 4;
            if (n == 0 || n > d.count || write_bulk_id(route, req.data, n) != ORPHEUS_OK) {
                *resp_flags = ORPHEUS_MSG_FLAG_ERROR;
            }
            return 0;
        }
        if (req.len < 4) { *resp_flags = ORPHEUS_MSG_FLAG_ERROR; return 0; }
        OrpheusValue v;
        if (d.type == ORPHEUS_VALUE_FLOAT) {
            v.type = ORPHEUS_VALUE_FLOAT;
            std::memcpy(&v.value.f32, req.data, 4);
        } else if (d.type == ORPHEUS_VALUE_INT) {
            v.type = ORPHEUS_VALUE_INT;
            std::memcpy(&v.value.i32, req.data, 4);
        } else if (d.type == ORPHEUS_VALUE_BOOL) {
            v.type = ORPHEUS_VALUE_BOOL;
            v.value.b = *reinterpret_cast<const uint8_t*>(req.data) != 0;
        } else {
            *resp_flags = ORPHEUS_MSG_FLAG_ERROR;
            return 0;
        }
        if (write_id(route, v) != ORPHEUS_OK) *resp_flags = ORPHEUS_MSG_FLAG_ERROR;
        return 0;
    }
    /* 读 */
    if (d.form == ORPHEUS_FORM_BULK) {
        size_t n = d.count;
        if (n == 0 || n * 4 > out_cap - kMsgHdrSize) { *resp_flags = ORPHEUS_MSG_FLAG_ERROR; return 0; }
        if (get_bulk_id(route, out + kMsgHdrSize, n) != ORPHEUS_OK) {
            *resp_flags = ORPHEUS_MSG_FLAG_ERROR;
            return 0;
        }
        *resp_words = static_cast<uint32_t>(n);
        return 0;
    }
    OrpheusValue v;
    if (read_id(route, &v) != ORPHEUS_OK) { *resp_flags = ORPHEUS_MSG_FLAG_ERROR; return 0; }
    if (v.type == ORPHEUS_VALUE_FLOAT) {
        std::memcpy(out + kMsgHdrSize, &v.value.f32, 4);
        *resp_words = 1;
    } else if (v.type == ORPHEUS_VALUE_INT) {
        std::memcpy(out + kMsgHdrSize, &v.value.i32, 4);
        *resp_words = 1;
    } else if (v.type == ORPHEUS_VALUE_BOOL) {
        out[kMsgHdrSize] = v.value.b ? 1 : 0;
        *resp_words = 1;
    } else {
        *resp_flags = ORPHEUS_MSG_FLAG_ERROR;
    }
    return 0;
}

size_t Runtime::build_notification(uint32_t route_id, uint32_t call_id,
                                   const void* data, size_t len, uint8_t* out, size_t out_cap) const {
    size_t words = (len + 3) / 4;
    if (out == nullptr || out_cap < kMsgHdrSize + words * 4) return 0;
    OrpheusMessageHeader* h = reinterpret_cast<OrpheusMessageHeader*>(out);
    h->route_id = route_id;
    h->bits = ORPHEUS_MSG_MAKE(ORPHEUS_MSG_NOTIFICATION, 0, call_id, static_cast<uint32_t>(words));
    if (words > 0) std::memcpy(out + kMsgHdrSize, data, len);
    return kMsgHdrSize + words * 4;
}

std::vector<const SlotEntry*> Runtime::probe_slots(const std::string& node_id) const {
    std::vector<const SlotEntry*> out;
    auto it = instances_.find(node_id);
    if (it == instances_.end()) return out;
    for (const auto& e : it->second->slots) {
        if (e.kind == ORPHEUS_SLOT_PROBE) out.push_back(&e);
    }
    return out;
}

int Runtime::resolve(uint32_t id, OrpheusResolvedData* out) const {
    if (out == nullptr) return -1;
    std::memset(out, 0, sizeof(*out));
    out->id = id;

    auto it = id_index_.find(id);
    if (it == id_index_.end()) {
        /* 模块包条目：用途=TUNE、槽=ORPHEUS_ID_SLOT_MODULE（不占数据点槽） */
        uint32_t module_id = ORPHEUS_ID_MODULE(id);
        if (ORPHEUS_ID_KIND(id) == ORPHEUS_ID_TUNE &&
            ORPHEUS_ID_SLOT(id) == ORPHEUS_ID_SLOT_MODULE) {
            for (const auto& m : plan_.modules) {
                if (m.id != module_id) continue;
                out->kind = ORPHEUS_ID_TUNE;
                out->form = ORPHEUS_FORM_MODULE;
                out->type = ORPHEUS_VALUE_BULK_REF;
                out->count = 1;
                out->module_id = module_id;
                out->slot = ORPHEUS_ID_SLOT_MODULE;
                out->name = m.path.c_str();
                auto ml = module_layout_.find(module_id);
                if (ml != module_layout_.end()) {
                    out->base = const_cast<uint8_t*>(state_arena_.data()) + ml->second.first;
                    out->byte_size = ml->second.second;
                }
                return ORPHEUS_OK;
            }
        }
        return ORPHEUS_ERR_NOT_FOUND;
    }

    const IdMapEntry* e = it->second;
    auto ii = instances_.find(e->node);
    if (ii == instances_.end()) return ORPHEUS_ERR_NOT_FOUND;
    const Instance& inst = *ii->second;
    auto si = inst.slot_index.find(e->key);
    if (si == inst.slot_index.end()) {
        if (e->kind == ORPHEUS_ID_CUSTOM) {
            /* CUSTOM 消息入口：无槽内存，但可按 ID 路由到组件 hook */
            out->kind = ORPHEUS_ID_CUSTOM;
            out->form = ORPHEUS_FORM_SCALAR;
            out->module_id = ORPHEUS_ID_MODULE(id);
            out->slot = ORPHEUS_ID_SLOT(id);
            out->node = e->node.c_str();
            out->key = e->key.c_str();
            out->name = e->name.c_str();
            return ORPHEUS_OK;
        }
        return ORPHEUS_ERR_NOT_FOUND;
    }
    const SlotEntry& slot = inst.slots[si->second];

    out->kind = e->kind;
    out->form = e->form;
    out->type = slot.type;
    out->count = slot.count;
    out->byte_size = slot.count * slot.size;
    out->module_id = ORPHEUS_ID_MODULE(id);
    out->slot = ORPHEUS_ID_SLOT(id);
    out->base = static_cast<char*>(inst.state) + slot.offset;
    out->offset = slot.offset;
    out->node = e->node.c_str();
    out->key = e->key.c_str();
    out->name = slot.name.empty() ? e->name.c_str() : slot.name.c_str();
    return ORPHEUS_OK;
}

int Runtime::resolve_all(std::vector<OrpheusResolvedData>* out) const {
    if (out == nullptr) return -1;
    out->clear();
    OrpheusResolvedData d;
    for (const auto& e : plan_.id_map) {
        if (resolve(e.id, &d) == ORPHEUS_OK) out->push_back(d);
    }
    for (const auto& m : plan_.modules) {
        if (m.path.empty()) continue;
        uint32_t mid = ORPHEUS_ID_MAKE(ORPHEUS_ID_TUNE, m.id, ORPHEUS_ID_SLOT_MODULE);
        if (resolve(mid, &d) == ORPHEUS_OK) out->push_back(d);
    }
    return ORPHEUS_OK;
}

int Runtime::write_id(uint32_t id, const OrpheusValue& value) {
    OrpheusResolvedData d;
    if (resolve(id, &d) != ORPHEUS_OK) return ORPHEUS_ERR_NOT_FOUND;
    if (d.kind == ORPHEUS_ID_PROBE || d.kind == ORPHEUS_ID_STATE) return ORPHEUS_ERR_INVALID_ARG;
    if (d.form == ORPHEUS_FORM_MODULE) return ORPHEUS_ERR_UNSUPPORTED;
    return set_parameter(d.node, d.key, value);
}

int Runtime::read_id(uint32_t id, OrpheusValue* value) {
    OrpheusResolvedData d;
    if (resolve(id, &d) != ORPHEUS_OK) return ORPHEUS_ERR_NOT_FOUND;
    if (d.form == ORPHEUS_FORM_MODULE) return ORPHEUS_ERR_UNSUPPORTED;
    return get_parameter(d.node, d.key, value);
}

int Runtime::write_bulk_id(uint32_t id, const void* data, size_t count) {
    OrpheusResolvedData d;
    if (resolve(id, &d) != ORPHEUS_OK) return ORPHEUS_ERR_NOT_FOUND;
    if (d.form != ORPHEUS_FORM_BULK) return ORPHEUS_ERR_INVALID_ARG;
    return write_bulk(d.node, d.key, data, count);
}

const OrpheusComponentInterface* Runtime::get_interface(const std::string& node_id) {
    auto it = instances_.find(node_id);
    if (it == instances_.end()) return nullptr;
    return it->second->interface_;
}

void Runtime::control_tick() {
    control_tick_for_task(nullptr);
}

void Runtime::control_tick_for_task(const std::string* task_id) {
    if (control_links_.empty()) return;
    /* 第一相：读 —— 全部源参数 → 快照（经 ABI get_parameter / 槽直读）。
       字符串源拷贝进预分配缓冲，process 路径零分配。 */
    for (auto& l : control_links_) {
        if (l.skip) continue;
        if (task_id != nullptr &&
            (plan_.node_configs[l.cfg->src_node].task != *task_id ||
             plan_.node_configs[l.cfg->dst_node].task != *task_id)) continue;
        l.read_ok = get_parameter(l.cfg->src_node, l.cfg->src_param, &l.snapshot) == ORPHEUS_OK;
        if (l.read_ok && l.snapshot.type == ORPHEUS_VALUE_STRING && !l.str_buf.empty()) {
            const char* s = l.snapshot.value.str != nullptr ? l.snapshot.value.str : "";
            size_t n = std::strlen(s);
            if (n >= l.str_buf.size()) n = l.str_buf.size() - 1;  /* 超长截断 */
            std::memcpy(l.str_buf.data(), s, n);
            l.str_buf[n] = '\0';
        }
    }
    /* 第二相：写 —— 快照 → 全部目标（经 ABI set_parameter / 槽直写）。
       读写分相保证顺序无关：多跳链每链固定 1 块延迟。 */
    for (auto& l : control_links_) {
        if (l.skip || !l.read_ok) continue;
        if (task_id != nullptr &&
            (plan_.node_configs[l.cfg->src_node].task != *task_id ||
             plan_.node_configs[l.cfg->dst_node].task != *task_id)) continue;
        OrpheusValue v = l.snapshot;
        if (v.type == ORPHEUS_VALUE_STRING && !l.str_buf.empty()) {
            v.value.str = l.str_buf.data();
        }
        set_parameter(l.cfg->dst_node, l.cfg->dst_param, v);
    }
}

void Runtime::commit_bulk() {
    /* 块边界提交：待写的 BULK 影子区一次性 memcpy 到 active（单控制写者假设） */
    for (auto& kv : bulk_pending_) {
        if (!kv.second) continue;
        auto sm = bulk_shadow_map_.find(kv.first);
        auto am = bulk_active_map_.find(kv.first);
        auto span_it = bulk_span_map_.find(kv.first);
        if (sm != bulk_shadow_map_.end() && am != bulk_active_map_.end() &&
            span_it != bulk_span_map_.end()) {
            std::memcpy(am->second, sm->second, span_it->second);
        }
        kv.second = false;
    }
}

void Runtime::task_bridge_push(TaskBridge& bridge) {
    const uint32_t frames = bridge.staging->frame_count;
    const uint64_t read = bridge.read_pos.load(std::memory_order_acquire);
    const uint64_t write = bridge.write_pos.load(std::memory_order_relaxed);
    if (frames > bridge.capacity_frames || write - read + frames > bridge.capacity_frames) {
        bridge.overruns.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const uint32_t slot = static_cast<uint32_t>((write + frame) % bridge.capacity_frames);
        std::memcpy(bridge.ring.get() + static_cast<size_t>(slot) * bridge.channels,
                    static_cast<const float*>(bridge.staging->data)
                        + static_cast<size_t>(frame) * bridge.channels,
                    bridge.channels * sizeof(float));
    }
    bridge.write_pos.store(write + frames, std::memory_order_release);
}

void Runtime::task_bridge_pop(TaskBridge& bridge) {
    const uint32_t wanted = bridge.consumer->frame_capacity;
    const uint64_t read = bridge.read_pos.load(std::memory_order_relaxed);
    const uint64_t write = bridge.write_pos.load(std::memory_order_acquire);
    const uint32_t available = static_cast<uint32_t>(write - read);
    const uint32_t copied = available < wanted ? available : wanted;
    float* output = static_cast<float*>(bridge.consumer->data);
    for (uint32_t frame = 0; frame < copied; ++frame) {
        const uint32_t slot = static_cast<uint32_t>((read + frame) % bridge.capacity_frames);
        std::memcpy(output + static_cast<size_t>(frame) * bridge.channels,
                    bridge.ring.get() + static_cast<size_t>(slot) * bridge.channels,
                    bridge.channels * sizeof(float));
    }
    if (copied < wanted) {
        std::memset(output + static_cast<size_t>(copied) * bridge.channels, 0,
                    static_cast<size_t>(wanted - copied) * bridge.channels * sizeof(float));
        bridge.underruns.fetch_add(1, std::memory_order_relaxed);
    }
    bridge.consumer->frame_count = wanted;
    bridge.read_pos.store(read + copied, std::memory_order_release);
}

void Runtime::update_task_bridge_probes(TaskBridge& bridge) {
    auto instance_it = instances_.find(bridge.consumer_node);
    if (instance_it == instances_.end() || instance_it->second->interface_->set_parameter == nullptr) return;
    OrpheusValue value;
    value.type = ORPHEUS_VALUE_INT;
    value.value.i32 = static_cast<int32_t>(
        bridge.write_pos.load(std::memory_order_acquire)
        - bridge.read_pos.load(std::memory_order_acquire));
    instance_it->second->interface_->set_parameter(instance_it->second->state, "level_frames", &value);
    value.value.i32 = static_cast<int32_t>(bridge.underruns.load(std::memory_order_relaxed));
    instance_it->second->interface_->set_parameter(instance_it->second->state, "underruns", &value);
    value.value.i32 = static_cast<int32_t>(bridge.overruns.load(std::memory_order_relaxed));
    instance_it->second->interface_->set_parameter(instance_it->second->state, "overruns", &value);
}

int Runtime::process_nodes(const std::vector<std::string>& execution_order,
                           const std::map<std::string, uint32_t>* periods,
                           uint64_t counter, uint32_t frame_count, bool task_mode) {
    OrpheusProcessContext ctx;
    ctx.scratch = nullptr;
    ctx.scratch_size = 0;
    ctx.timestamp = 0.0;

    for (const auto& node_id : execution_order) {
        const NodeConfig& cfg = plan_.node_configs[node_id];
        uint32_t period = cfg.period > 0 ? cfg.period : cfg.divisor;
        if (periods != nullptr) {
            const auto period_it = periods->find(node_id);
            if (period_it != periods->end()) period = period_it->second;
        }
        if (period > 1 && (counter + 1) % period != 0) {
            continue;
        }
        auto reads = task_bridge_reads_.find(node_id);
        if (reads != task_bridge_reads_.end()) {
            for (TaskBridge* bridge : reads->second) {
                if (task_mode || !bridge->legacy_rate_bridge) {
                    task_bridge_pop(*bridge);
                    update_task_bridge_probes(*bridge);
                }
            }
        }
        Instance& inst = *instances_[node_id];
        ctx.state = inst.state;
        ctx.frame_count = cfg.frames > 0 ? cfg.frames : frame_count;
        ctx.inputs = inst.inputs.empty() ? nullptr : const_cast<const OrpheusBuffer**>(inst.inputs.data());
        ctx.outputs = inst.outputs.empty() ? nullptr : inst.outputs.data();
        ctx.input_count = static_cast<uint32_t>(inst.inputs.size());
        ctx.output_count = static_cast<uint32_t>(inst.outputs.size());
        ctx.sample_rate = cfg.sample_rate > 0 ? cfg.sample_rate : plan_.sample_rate;

        int result = inst.interface_->process(inst.state, &ctx);
        if (result != ORPHEUS_OK) {
            return result;
        }
        // rate-bridge：生产者触发后，把 staging 里的新鲜块按写游标滚入桥接 buffer
        auto bc = bridge_copies_.find(node_id);
        if (!task_mode && bc != bridge_copies_.end()) {
            for (auto& cp : bc->second) {
                float* dst = static_cast<float*>(cp.bridge->data)
                             + static_cast<size_t>(cp.cursor) * cp.channels;
                const size_t n = static_cast<size_t>(cp.frames) * cp.channels;
                std::memcpy(dst, cp.staging->data, n * sizeof(float));
                cp.cursor = (cp.cursor + cp.frames) % cp.bridge->frame_capacity;
            }
        }
        auto writes = task_bridge_writes_.find(node_id);
        if (writes != task_bridge_writes_.end()) {
            for (TaskBridge* bridge : writes->second) {
                if (task_mode || !bridge->legacy_rate_bridge) task_bridge_push(*bridge);
            }
        }
    }
    return 0;
}

int Runtime::process_block(uint32_t frame_count) {
    commit_bulk();
    const int result = process_nodes(plan_.execution_order, nullptr, block_counter_, frame_count);
    if (result != 0) return result;
    control_tick();
    block_counter_++;
    return 0;
}

int Runtime::process_task(const std::string& task_id, uint32_t frame_count) {
    const TaskConfig* task = nullptr;
    for (const auto& candidate : plan_.tasks) {
        if (candidate.id == task_id) {
            task = &candidate;
            break;
        }
    }
    if (task == nullptr) return ORPHEUS_ERR_NOT_FOUND;

    commit_bulk();
    uint64_t& counter = task_counters_[task_id];
    const int result = process_nodes(task->execution_order, &task->periods, counter, frame_count, true);
    if (result != 0) return result;
    control_tick_for_task(&task_id);
    counter++;
    return ORPHEUS_OK;
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
