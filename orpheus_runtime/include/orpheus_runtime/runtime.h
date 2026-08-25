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
    // 双 bank：写入影子区，块边界（process_block 开始）memcpy 提交到 active——glitch-free。
    int write_bulk(const std::string& node_id, const std::string& key,
                   const void* data, size_t count);

    // v2.1 BULK 读回（active bank）：越界检查后仅 memcpy，体现高速大块特性。
    int get_bulk(const std::string& node_id, const std::string& key, void* out, size_t count);
    int get_bulk_id(uint32_t id, void* out, size_t count);

    // node/key → 32 位数据 ID（反查，GETBULK 等使用）
    bool lookup_id(const std::string& node_id, const std::string& key, uint32_t* out_id) const;

    // v2.2 统一 hook（外部注册优先于组件接口 hook，最后默认语义）
    int register_hook(uint32_t id, OrpheusHookFn fn, void* ctx);

    // v2.2 二进制消息：CALL → RESPONSE（同步返回）；NOTIFICATION → 单向分发（out_len=0）
    int message(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap, size_t* out_len);

    // 构造 NOTIFICATION 帧（异步结果/事件向外推送）；返回帧长（0=缓冲不足）
    size_t build_notification(uint32_t route_id, uint32_t call_id, const void* data, size_t len,
                              uint8_t* out, size_t out_cap) const;

    // v2 探针发现：返回某节点的 PROBE 槽列表（替代宿主按组件名/描述符猜测）。
    std::vector<const SlotEntry*> probe_slots(const std::string& node_id) const;

    // v2.1 数据 ID 解析（内存透明）：ID → 类型/长度/基址/偏移。
    // 数据点返回真实地址（base = 实例状态块 + 槽偏移）；模块包返回元数据（base=NULL 直到模块连续分配）。
    int resolve(uint32_t id, OrpheusResolvedData* out) const;

    // 全表 dump：所有数据点 + 模块包条目（等价生成路径的 orpheus_id_map）。
    int resolve_all(std::vector<OrpheusResolvedData>* out) const;

    // v2.1 按 ID 实时控制（RTC 通道）：方向只在接口；
    // PROBE/STATE 拒写、命令拒读、模块包不直接读写（整块读回待 get_bulk）。
    int write_id(uint32_t id, const OrpheusValue& value);
    int read_id(uint32_t id, OrpheusValue* value);
    int write_bulk_id(uint32_t id, const void* data, size_t count);

    // Access the component interface of a loaded node (metadata/introspection).
    const OrpheusComponentInterface* get_interface(const std::string& node_id);

    // Execute one block.
    int process_block(uint32_t frame_count);

    // 控制链路：块边界两相快照（先读全部源、再写全部目标），每图块一次。
    // 读快照在上一个块末完成，故每条链固定 1 块延迟，闭环合法（无代数环）。
    void control_tick();

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
    std::vector<std::unique_ptr<OrpheusBuffer>> discard_buffers_;
    std::vector<float> discard_memory_;
    std::vector<uint8_t> state_arena_;   // v2：统一内存拼接（每实例一块连续切片）
    std::map<uint32_t, const IdMapEntry*> id_index_;   // 数据 ID → plan.id_map 条目
    std::map<std::string, uint32_t> key_to_id_;        // "node\x1fkey" → 数据 ID
    std::map<uint32_t, std::pair<size_t, size_t>> module_layout_;  // 模块 id → (arena 基址, 字节数)
    std::vector<uint8_t> bulk_shadow_;                 // BULK 槽影子区（双 bank 写侧）
    std::map<std::string, uint8_t*> bulk_shadow_map_;  // "node\x1fkey" -> 影子指针
    std::map<std::string, uint8_t*> bulk_active_map_;  // "node\x1fkey" -> active 指针
    std::map<std::string, size_t> bulk_span_map_;      // "node\x1fkey" -> 字节数
    std::map<std::string, bool> bulk_pending_;         // 待提交标志（块边界提交）
    struct RegisteredHook { OrpheusHookFn fn; void* ctx; };
    std::map<uint32_t, RegisteredHook> hooks_;         // 外部注册 hook（按 route_id）
    uint64_t block_counter_ = 0;  // for rate-divisor scheduling

    // 控制链路运行态：快照与字符串缓冲在 load_plan 预分配，process 路径零 malloc。
    struct ControlLinkState {
        const ControlLinkConfig* cfg = nullptr;  // 指向 plan_.control_links（加载后不变）
        bool skip = false;       // count>1 的数值数组链本期不执行（编译期已做形状校验）
        bool read_ok = false;    // 本 tick 源读取是否成功
        OrpheusValue snapshot{}; // 数值/布尔快照
        std::vector<char> str_buf;  // 字符串快照预分配缓冲（string 透传用）
    };
    std::vector<ControlLinkState> control_links_;

    int msg_default(uint32_t route, const OrpheusBlob& req, bool write,
                    uint8_t* out, size_t out_cap, uint32_t* resp_words, uint32_t* resp_flags);

    int prepare_instance(Instance& inst, const NodeConfig& cfg);
};

} // namespace orpheus

#endif
