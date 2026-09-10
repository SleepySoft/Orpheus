#ifndef ORPHEUS_RUNTIME_RUNTIME_BRIDGE_H
#define ORPHEUS_RUNTIME_RUNTIME_BRIDGE_H

#include "orpheus_bridge_endpoint.h"
#include "orpheus_runtime/plan.h"
#include "orpheus_runtime/runtime.h"

#include <atomic>

namespace orpheus {

class RuntimeBridgeEndpoint {
public:
    RuntimeBridgeEndpoint(Runtime& runtime, const Plan& plan, std::atomic<bool>& running);

    int serve_stdio();
    OrpheusBridgeEndpoint* endpoint() { return &endpoint_; }

private:
    struct Context {
        Runtime* runtime;
        const Plan* plan;
        std::atomic<bool>* running;
    } context_;
    OrpheusBridgeEndpoint endpoint_{};

    static int dispatch(void* context, const uint8_t* request, size_t request_len,
                        uint8_t* response, size_t response_cap, size_t* response_len);
    static size_t map_count(void* context);
    static int map_get(void* context, size_t index, OrpheusBridgeMapEntry* output);
    static void request_stop(void* context);
};

} // namespace orpheus

#endif /* ORPHEUS_RUNTIME_RUNTIME_BRIDGE_H */
