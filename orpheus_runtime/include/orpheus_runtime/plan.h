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
    uint32_t divisor = 1;   // rate divisor: node runs when (block_counter+1) % divisor == 0
    uint32_t frames = 0;    // processing quantum per firing (0 = plan block_size)
};

struct BufferConfig {
    std::string id;
    std::string from;
    std::string to;
    std::string sample_format;
    uint32_t channels;
    uint32_t frame_count;
};

struct ConnectionConfig {
    std::string from;
    std::string to;
    std::string buffer;
};

struct Plan {
    uint32_t abi_version = 1;
    uint32_t sample_rate = 48000;
    uint32_t block_size = 128;
    std::string task_id;
    std::vector<std::string> nodes;
    std::vector<std::string> execution_order;
    std::map<std::string, NodeConfig> node_configs;
    std::map<std::string, BufferConfig> buffers;
    std::vector<ConnectionConfig> connections;

    static Plan load_from_file(const std::string& path);
};

} // namespace orpheus

#endif
