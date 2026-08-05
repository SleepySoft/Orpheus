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

struct Instance {
    std::string node_id;
    const OrpheusComponentInterface* interface_;
    void* state;
    std::vector<OrpheusBuffer*> inputs;   // indexed by input_ports order
    std::vector<OrpheusBuffer*> outputs;  // indexed by output_ports order
    std::map<std::string, size_t> input_index;   // port id -> slot
    std::map<std::string, size_t> output_index;
};

class Runtime {
public:
    Runtime();
    ~Runtime();

    // Load execution plan and component libraries from component_dir.
    int load_plan(const Plan& plan, const std::string& component_dir);

    // Set a parameter on a node.
    int set_parameter(const std::string& node_id, const std::string& param_id, const OrpheusValue& value);

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

    int prepare_instance(Instance& inst, const NodeConfig& cfg);
};

} // namespace orpheus

#endif
