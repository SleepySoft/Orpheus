#ifndef ORPHEUS_RUNTIME_PLAN_H
#define ORPHEUS_RUNTIME_PLAN_H

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace orpheus {

struct NodeConfig {
    std::string id;
    std::string component;
    std::string version;
    std::string task;
    std::map<std::string, std::string> params; // all params as strings
    std::vector<std::string> input_ports;      // ordered port ids for buffer binding
    std::vector<std::string> output_ports;
    std::map<std::string, uint32_t> output_port_block_sizes;
    std::map<std::string, uint32_t> output_port_channels;  // per-output-port resolved block size
    uint32_t divisor = 1;   // rate divisor: node runs when (block_counter+1) % divisor == 0
    uint32_t period = 0;    // 静态调度周期（主 tick 数）；0 = 回退 divisor（旧 plan）
    uint32_t block_size = 0;  // node rate-domain scheduling quantum (0 = plan fallback)
    uint32_t frames = 0;    // samples to call process with per firing (0 = plan block_size)
    uint32_t sample_rate = 0;  // node effective sample rate (0 = inherit plan rate)
};

struct BufferConfig {
    std::string id;
    std::string from;
    std::string to;
    std::string sample_format;
    uint32_t channels;
    uint32_t frame_count;
    bool rate_bridge = false;  // 合流桥接 buffer：深度=合流量子，骨架按写游标滚动填充
    bool task_bridge = false;  // 跨 Task SPSC：生产任务写、消费任务触发前读取
    uint32_t producer_frames = 0;
    uint32_t capacity_frames = 0;
};

struct ConnectionConfig {
    std::string from;
    std::string to;
    std::string buffer;
};

struct ModuleConfig {
    std::string path;
    uint32_t id = 0;
    std::vector<std::pair<std::string, uint32_t>> leaves;  // (node, slot)
};

struct IdMapEntry {
    uint32_t id = 0;
    std::string node;
    std::string key;
    uint32_t kind = 0;    // OrpheusIdKind（用途）
    uint32_t form = 0;    // OrpheusDataForm（形式）
    std::string type;     // "float"/"int"/"bool"/"string"
    uint32_t count = 1;
    std::string name;     // 中文显示名
    bool runtime = false; // 运行期槽（bulk_slots）
    bool double_bank = false;  // BULK 双 bank 生效（工程 auto/on/off × 组件声明）
};

// 控制链路：源节点参数值 → 目标节点参数（块边界两相快照投递，每链 1 块延迟）
struct ControlLinkConfig {
    std::string src_node;
    std::string src_param;
    std::string dst_node;
    std::string dst_param;
    std::string type;              // "float"/"int"/"bool"/"string"（编译期已严格匹配）
    std::vector<uint32_t> shape;   // 求值后的维度（空 = 标量）
    uint32_t count = 1;            // shape 各维乘积（标量为 1）
};

struct TaskConfig {
    std::string id;
    std::string name;
    uint32_t sample_rate = 48000;
    uint32_t block_size = 128;
    int32_t priority = 0;
    uint32_t schedule_tick = 0;
    std::vector<std::string> nodes;
    std::vector<std::string> execution_order;
    std::map<std::string, uint32_t> periods;
};

struct Plan {
    uint32_t abi_version = 1;
    uint32_t sample_rate = 48000;
    uint32_t block_size = 128;
    uint32_t buffer_size = 0;  // async ring buffer capacity (0 = auto)
    uint32_t duration_frames = 0;  // 离线宿主运行时长提示（0=默认 10s）
    uint32_t schedule_tick = 0;    // 静态调度主步长（图速率帧）；0=回退 block_size（旧 plan）
    std::string task_id;
    std::vector<TaskConfig> tasks;
    std::vector<std::string> nodes;
    std::vector<std::string> execution_order;
    std::map<std::string, NodeConfig> node_configs;
    std::map<std::string, BufferConfig> buffers;
    std::vector<ConnectionConfig> connections;
    std::vector<ModuleConfig> modules;
    std::vector<IdMapEntry> id_map;
    std::vector<ControlLinkConfig> control_links;

    static Plan load_from_file(const std::string& path);
};

} // namespace orpheus

#endif
