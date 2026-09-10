#ifndef ORPHEUS_BRIDGE_ENDPOINT_H
#define ORPHEUS_BRIDGE_ENDPOINT_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORPHEUS_BRIDGE_PROTOCOL_VERSION 1u
#define ORPHEUS_BRIDGE_SYSTEM_PREFIX 0xFFFF0000u
#define ORPHEUS_BRIDGE_ROUTE_HELLO    (ORPHEUS_BRIDGE_SYSTEM_PREFIX | 0x0001u)
#define ORPHEUS_BRIDGE_ROUTE_IDENTITY (ORPHEUS_BRIDGE_SYSTEM_PREFIX | 0x0002u)
#define ORPHEUS_BRIDGE_ROUTE_STOP     (ORPHEUS_BRIDGE_SYSTEM_PREFIX | 0x0003u)
#define ORPHEUS_BRIDGE_ROUTE_STATS    (ORPHEUS_BRIDGE_SYSTEM_PREFIX | 0x0004u)
#define ORPHEUS_BRIDGE_ROUTE_MAP      (ORPHEUS_BRIDGE_SYSTEM_PREFIX | 0x0005u)

#define ORPHEUS_BRIDGE_CAP_HALF_DUPLEX      (1u << 0)
#define ORPHEUS_BRIDGE_CAP_FULL_DUPLEX      (1u << 1)
#define ORPHEUS_BRIDGE_CAP_UNSOLICITED      (1u << 2)
#define ORPHEUS_BRIDGE_CAP_PIPELINED_CALLS  (1u << 3)
#define ORPHEUS_BRIDGE_CAP_FLOW_CONTROL     (1u << 4)
#define ORPHEUS_BRIDGE_CAP_MAP              (1u << 5)
#define ORPHEUS_BRIDGE_CAP_STOP             (1u << 6)

#define ORPHEUS_BRIDGE_KIND_DYNAMIC_RUNTIME 1u
#define ORPHEUS_BRIDGE_KIND_GENERATED_APP   2u
#define ORPHEUS_BRIDGE_KIND_GENERATED_DSP   3u

#define ORPHEUS_BRIDGE_IDENTITY_FLAG_WRITABLE (1u << 0)
#define ORPHEUS_BRIDGE_MAX_MESSAGE (8u + 1023u * 4u)

typedef struct {
    uint32_t protocol_version;
    uint32_t abi_version;
    uint32_t capabilities;
    uint32_t max_message;
    uint32_t endpoint_kind;
    uint32_t reserved;
} OrpheusBridgeHello;

typedef struct {
    uint64_t graph_hash;
    uint64_t plan_hash;
    uint64_t id_map_hash;
    uint32_t sample_rate;
    uint32_t block_size;
    uint32_t id_count;
    uint32_t flags;
} OrpheusBridgeIdentity;

typedef struct {
    uint64_t calls;
    uint64_t errors;
    uint64_t notifications;
    uint64_t dropped;
} OrpheusBridgeStats;

typedef struct {
    uint32_t start;
    uint32_t limit;
} OrpheusBridgeMapRequest;

typedef struct {
    uint32_t start;
    uint32_t total;
    uint32_t count;
    uint32_t reserved;
} OrpheusBridgeMapPage;

typedef struct {
    uint32_t id;
    uint32_t kind;
    uint32_t form;
    uint32_t type;
    uint32_t count;
    uint32_t byte_size;
    uint32_t module_id;
    uint32_t slot;
} OrpheusBridgeMapEntry;

typedef int (*OrpheusBridgeDispatchFn)(
    void* context,
    const uint8_t* request,
    size_t request_len,
    uint8_t* response,
    size_t response_cap,
    size_t* response_len);

typedef size_t (*OrpheusBridgeMapCountFn)(void* context);
typedef int (*OrpheusBridgeMapGetFn)(
    void* context, size_t index, OrpheusBridgeMapEntry* entry);
typedef void (*OrpheusBridgeStopFn)(void* context);

typedef struct {
    void* context;
    OrpheusBridgeDispatchFn dispatch;
    OrpheusBridgeMapCountFn map_count;
    OrpheusBridgeMapGetFn map_get;
    OrpheusBridgeStopFn request_stop;
} OrpheusBridgeBackend;

typedef struct {
    OrpheusBridgeBackend backend;
    OrpheusBridgeHello hello;
    OrpheusBridgeIdentity identity;
    OrpheusBridgeStats stats;
    bool stop_requested;
} OrpheusBridgeEndpoint;

void orpheus_bridge_endpoint_init(
    OrpheusBridgeEndpoint* endpoint,
    const OrpheusBridgeBackend* backend,
    const OrpheusBridgeHello* hello,
    const OrpheusBridgeIdentity* identity);

int orpheus_bridge_endpoint_process(
    OrpheusBridgeEndpoint* endpoint,
    const uint8_t* request,
    size_t request_len,
    uint8_t* response,
    size_t response_cap,
    size_t* response_len);

bool orpheus_bridge_endpoint_should_stop(const OrpheusBridgeEndpoint* endpoint);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_BRIDGE_ENDPOINT_H */
