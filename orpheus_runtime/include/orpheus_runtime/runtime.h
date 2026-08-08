#ifndef ORPHEUS_RUNTIME_RUNTIME_H
#define ORPHEUS_RUNTIME_RUNTIME_H

#include "orpheus_abi.h"
#include "orpheus_runtime/loader.h"
#include "orpheus_runtime/plan.h"

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

namespace orpheus {

struct SlotEntry {
    std::string key;
    std::string name;
    OrpheusSlotKind kind;
    OrpheusValueType type;
    size_t offset;
    size_t size;
    uint32_t count;
    float min_f32 = 0.0f, max_f32 = 0.0f;
    int32_t min_i32 = 0, max_i32 = 0;
    std::string unit;
    OrpheusUpdatePolicy update_policy = ORPHEUS_UPDATE_IMMEDIATE;
    uint32_t flags = 0;
};

struct Instance {
    std::string node_id;
    const OrpheusComponentInterface* interface_;
    void* state;
    size_t state_size = 0;
    std::vector<OrpheusBuffer*> inputs;   // indexed by input_ports order
    std::vector<OrpheusBuffer*> outputs;  // indexed by output_ports order
    std::map<std::string, size_t> input_index;   // port id -> slot
    std::map<std::string, size_t> output_index;
    std::vector<SlotEntry> slots;          // v2 注册的资源槽（SlotMap[instance]）
    std::map<std::string, size_t> slot_index;  // slot key -> slots 下标
};

class Runtime {
public:
    Runtime();
    ~Runtime();

    // Load execution plan and component libraries from component_dir.
    int load_plan(const Plan& plan, const std::string& component_dir);

    // Set a parameter on a node.
    int set_parameter(const std::string& node_id, const std::string& param_id, const OrpheusValue& value);

    // Get a parameter (e.g. probe readback values) from a node.
    int get_parameter(const std::string& node_id, const std::string& param_id, OrpheusValue* value);

    // v2 BULK 直写：把大块数据（系数/查表）写入注册的 BULK 数组槽，带边界校验。
    int write_bulk(const std::string& node_id, const std::string& key,
                   const void* data, size_t count);

    // v2 探针发现：返回某节点的 PROBE 槽列表（替代宿主按组件名/描述符猜测）。
    std::vector<const SlotEntry*> probe_slots(const std::string& node_id) const;

    // v2.1 数据 ID 解析（内存透明）：ID → 类型/长度/基址/偏移。
    // 数据点返回真实地址（base = 实例状态块 + 槽偏移）；模块包返回元数据（base=NULL 直到模块连续分配）。
    int resolve(uint32_t id, OrpheusResolvedData* out) const;

    // 全表 dump：所有数据点 + 模块包条目（等价生成路径的 orpheus_id_map）。
    int resolve_all(std::vector<OrpheusResolvedData>* out) const;

    // Access the component interface of a loaded node (metadata/introspection).
    const OrpheusComponentInterface* get_interface(const std::string& node_id);

    // Execute one block.
    int process_block(uint32_t frame_count);

    // Process entire WAV file: input_path -> output_path.
    int process_wav(const std::string& input_path, const std::string& output_path);

    // Access buffers for I/O injection / monitoring.
    OrpheusBuffer* get_input_buffer(const std::string& node_id, const std::string& port_id);
    OrpheusBuffer* get_output_buffer(const std::string& node_id, const std::string& port_id);

private:
    Plan plan_;
    ComponentLoader loader_;
    std::map<std::string, const OrpheusComponentInterface*> interfaces_;
    std::map<std::string, std::unique_ptr<Instance>> instances_;
    std::map<std::string, std::unique_ptr<OrpheusBuffer>> buffers_;
    std::vector<float> buffer_memory_;
    std::vector<uint8_t> state_arena_;   // v2：统一内存拼接（每实例一块连续切片）
    std::map<uint32_t, const IdMapEntry*> id_index_;   // 数据 ID → plan.id_map 条目
    uint64_t block_counter_ = 0;  // for rate-divisor scheduling

    int prepare_instance(Instance& inst, const NodeConfig& cfg);
};

} // namespace orpheus

#endif
