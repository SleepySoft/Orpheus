#include "orpheus_runtime/runtime_bridge.h"
#include "orpheus_bridge_stdio.h"

namespace orpheus {

static uint32_t type_value(const std::string& type) {
    if (type == "int") return ORPHEUS_VALUE_INT;
    if (type == "bool") return ORPHEUS_VALUE_BOOL;
    if (type == "string") return ORPHEUS_VALUE_STRING;
    return ORPHEUS_VALUE_FLOAT;
}

static uint32_t type_size(const std::string& type) {
    return (type == "bool" || type == "string") ? 1u : 4u;
}

RuntimeBridgeEndpoint::RuntimeBridgeEndpoint(
    Runtime& runtime, const Plan& plan, std::atomic<bool>& running)
    : context_{&runtime, &plan, &running} {
    OrpheusBridgeBackend backend{};
    backend.context = &context_;
    backend.dispatch = dispatch;
    backend.map_count = map_count;
    backend.map_get = map_get;
    backend.request_stop = request_stop;

    OrpheusBridgeHello hello{};
    hello.endpoint_kind = ORPHEUS_BRIDGE_KIND_DYNAMIC_RUNTIME;
    OrpheusBridgeIdentity identity{};
    identity.graph_hash = plan.bridge_graph_hash;
    identity.plan_hash = plan.bridge_plan_hash;
    identity.id_map_hash = plan.bridge_id_map_hash;
    identity.sample_rate = plan.sample_rate;
    identity.block_size = plan.schedule_tick > 0 ? plan.schedule_tick : plan.block_size;
    identity.flags = ORPHEUS_BRIDGE_IDENTITY_FLAG_WRITABLE;
    orpheus_bridge_endpoint_init(&endpoint_, &backend, &hello, &identity);
}

int RuntimeBridgeEndpoint::dispatch(
    void* context, const uint8_t* request, size_t request_len,
    uint8_t* response, size_t response_cap, size_t* response_len) {
    Context* bridge = static_cast<Context*>(context);
    return bridge->runtime->message(
        request, request_len, response, response_cap, response_len);
}

size_t RuntimeBridgeEndpoint::map_count(void* context) {
    return static_cast<Context*>(context)->plan->id_map.size();
}

int RuntimeBridgeEndpoint::map_get(
    void* context, size_t index, OrpheusBridgeMapEntry* output) {
    Context* bridge = static_cast<Context*>(context);
    if (output == nullptr || index >= bridge->plan->id_map.size()) {
        return ORPHEUS_ERR_NOT_FOUND;
    }
    const IdMapEntry& source = bridge->plan->id_map[index];
    output->id = source.id;
    output->kind = source.kind;
    output->form = source.form;
    output->type = type_value(source.type);
    output->count = source.count;
    output->byte_size = source.kind == ORPHEUS_ID_CUSTOM
        ? 0u : source.count * type_size(source.type);
    output->module_id = ORPHEUS_ID_MODULE(source.id);
    output->slot = ORPHEUS_ID_SLOT(source.id);
    return ORPHEUS_OK;
}

void RuntimeBridgeEndpoint::request_stop(void* context) {
    static_cast<Context*>(context)->running->store(false);
}

int RuntimeBridgeEndpoint::serve_stdio() {
    return orpheus_bridge_stdio_serve(&endpoint_, stdin, stdout);
}

} // namespace orpheus
