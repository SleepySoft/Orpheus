#include "orpheus_bridge_endpoint.h"

#include <string.h>

typedef struct {
    int stop_called;
    uint32_t value;
} TestBackend;

static int dispatch(void* context, const uint8_t* request, size_t request_len,
                    uint8_t* response, size_t response_cap, size_t* response_len) {
    TestBackend* backend = (TestBackend*)context;
    OrpheusMessageHeader header;
    if (request_len < sizeof(header) || response_cap < sizeof(header) + 4u) return -1;
    memcpy(&header, request, sizeof(header));
    header.bits = ORPHEUS_MSG_MAKE(
        ORPHEUS_MSG_RESPONSE, 0, ORPHEUS_MSG_CALL_ID(&header), 1u);
    memcpy(response, &header, sizeof(header));
    memcpy(response + sizeof(header), &backend->value, 4u);
    *response_len = sizeof(header) + 4u;
    return ORPHEUS_OK;
}

static size_t map_count(void* context) {
    (void)context;
    return 1u;
}

static int map_get(void* context, size_t index, OrpheusBridgeMapEntry* entry) {
    (void)context;
    if (index != 0u) return ORPHEUS_ERR_NOT_FOUND;
    *entry = (OrpheusBridgeMapEntry){0x00010001u, ORPHEUS_ID_RTC,
        ORPHEUS_FORM_SCALAR, ORPHEUS_VALUE_FLOAT, 1u, 4u, 1u, 1u};
    return ORPHEUS_OK;
}

static void request_stop(void* context) {
    ((TestBackend*)context)->stop_called = 1;
}

static size_t make_call(uint32_t route, uint16_t call_id, const void* payload,
                        size_t payload_len, uint8_t* output) {
    OrpheusMessageHeader header = {
        route,
        ORPHEUS_MSG_MAKE(ORPHEUS_MSG_CALL, 0, call_id,
            (uint32_t)((payload_len + 3u) / 4u)),
    };
    const size_t padded = ((payload_len + 3u) / 4u) * 4u;
    memcpy(output, &header, sizeof(header));
    memset(output + sizeof(header), 0, padded);
    if (payload_len > 0u) memcpy(output + sizeof(header), payload, payload_len);
    return sizeof(header) + padded;
}

int main(void) {
    TestBackend state = {0, 0x12345678u};
    OrpheusBridgeBackend backend = {
        &state, dispatch, map_count, map_get, request_stop,
    };
    OrpheusBridgeHello hello = {
        0u, 0u, 0u, 0u, ORPHEUS_BRIDGE_KIND_GENERATED_APP, 0u,
    };
    OrpheusBridgeIdentity identity = {
        0x11u, 0x22u, 0x33u, 48000u, 128u, 0u,
        ORPHEUS_BRIDGE_IDENTITY_FLAG_WRITABLE,
    };
    OrpheusBridgeEndpoint endpoint;
    orpheus_bridge_endpoint_init(&endpoint, &backend, &hello, &identity);

    uint8_t request[64];
    uint8_t response[ORPHEUS_BRIDGE_MAX_MESSAGE];
    size_t request_len;
    size_t response_len;
    OrpheusMessageHeader response_header;

    request_len = make_call(ORPHEUS_BRIDGE_ROUTE_HELLO, 7u, NULL, 0u, request);
    if (orpheus_bridge_endpoint_process(&endpoint, request, request_len,
            response, sizeof(response), &response_len) != ORPHEUS_OK) return 1;
    memcpy(&response_header, response, sizeof(response_header));
    if (ORPHEUS_MSG_TYPE(&response_header) != ORPHEUS_MSG_RESPONSE ||
        ORPHEUS_MSG_CALL_ID(&response_header) != 7u) return 2;
    OrpheusBridgeHello got_hello;
    memcpy(&got_hello, response + sizeof(response_header), sizeof(got_hello));
    if (got_hello.protocol_version != ORPHEUS_BRIDGE_PROTOCOL_VERSION ||
        (got_hello.capabilities & ORPHEUS_BRIDGE_CAP_MAP) == 0u) return 3;

    request_len = make_call(ORPHEUS_BRIDGE_ROUTE_IDENTITY, 8u, NULL, 0u, request);
    if (orpheus_bridge_endpoint_process(&endpoint, request, request_len,
            response, sizeof(response), &response_len) != ORPHEUS_OK) return 4;
    OrpheusBridgeIdentity got_identity;
    memcpy(&got_identity, response + sizeof(response_header), sizeof(got_identity));
    if (got_identity.id_map_hash != 0x33u || got_identity.id_count != 1u) return 5;

    OrpheusBridgeMapRequest map_request = {0u, 8u};
    request_len = make_call(ORPHEUS_BRIDGE_ROUTE_MAP, 9u,
        &map_request, sizeof(map_request), request);
    if (orpheus_bridge_endpoint_process(&endpoint, request, request_len,
            response, sizeof(response), &response_len) != ORPHEUS_OK) return 6;
    OrpheusBridgeMapPage page;
    memcpy(&page, response + sizeof(response_header), sizeof(page));
    if (page.total != 1u || page.count != 1u) return 7;

    request_len = make_call(0x00010001u, 10u, NULL, 0u, request);
    if (orpheus_bridge_endpoint_process(&endpoint, request, request_len,
            response, sizeof(response), &response_len) != ORPHEUS_OK) return 8;
    uint32_t value;
    memcpy(&value, response + sizeof(response_header), sizeof(value));
    if (value != state.value) return 9;

    request_len = make_call(ORPHEUS_BRIDGE_ROUTE_STOP, 11u, NULL, 0u, request);
    if (orpheus_bridge_endpoint_process(&endpoint, request, request_len,
            response, sizeof(response), &response_len) != ORPHEUS_OK) return 10;
    if (!state.stop_called || !orpheus_bridge_endpoint_should_stop(&endpoint)) return 11;
    return 0;
}