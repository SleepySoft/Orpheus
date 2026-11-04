#include "orpheus_bridge_endpoint.h"

#include <string.h>

static int bridge_response(
    const OrpheusMessageHeader* request,
    uint32_t flags,
    const void* payload,
    size_t payload_len,
    uint8_t* response,
    size_t response_cap,
    size_t* response_len) {
    const size_t words = (payload_len + 3u) / 4u;
    const size_t padded_len = words * 4u;
    const size_t total = sizeof(OrpheusMessageHeader) + padded_len;
    if (words > 1023u || total > response_cap) return ORPHEUS_ERR_OUT_OF_MEMORY;

    OrpheusMessageHeader header;
    header.route_id = request->route_id;
    header.bits = ORPHEUS_MSG_MAKE(
        ORPHEUS_MSG_RESPONSE, flags, ORPHEUS_MSG_CALL_ID(request), (uint32_t)words);
    memcpy(response, &header, sizeof(header));
    if (payload_len > 0u && payload != NULL) {
        memcpy(response + sizeof(header), payload, payload_len);
    }
    if (padded_len > payload_len) {
        memset(response + sizeof(header) + payload_len, 0, padded_len - payload_len);
    }
    *response_len = total;
    return ORPHEUS_OK;
}

static int bridge_map_response(
    OrpheusBridgeEndpoint* endpoint,
    const OrpheusMessageHeader* request,
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* response,
    size_t response_cap,
    size_t* response_len) {
    OrpheusBridgeMapRequest map_request = {0u, 0u};
    if (payload_len >= sizeof(map_request)) {
        memcpy(&map_request, payload, sizeof(map_request));
    }
    const size_t total_entries = endpoint->backend.map_count != NULL
        ? endpoint->backend.map_count(endpoint->backend.context) : 0u;
    if (map_request.start > total_entries) map_request.start = (uint32_t)total_entries;

    const size_t header_size = sizeof(OrpheusBridgeMapPage);
    const size_t max_entries = response_cap > sizeof(OrpheusMessageHeader) + header_size
        ? (response_cap - sizeof(OrpheusMessageHeader) - header_size)
            / sizeof(OrpheusBridgeMapEntry)
        : 0u;
    size_t limit = map_request.limit > 0u ? map_request.limit : max_entries;
    if (limit > max_entries) limit = max_entries;
    if (limit > total_entries - map_request.start) {
        limit = total_entries - map_request.start;
    }

    uint8_t payload_out[ORPHEUS_BRIDGE_MAX_MESSAGE - sizeof(OrpheusMessageHeader)];
    OrpheusBridgeMapPage page = {
        map_request.start,
        (uint32_t)total_entries,
        (uint32_t)limit,
        0u,
    };
    memcpy(payload_out, &page, sizeof(page));
    size_t count = 0u;
    for (; count < limit; ++count) {
        OrpheusBridgeMapEntry entry;
        if (endpoint->backend.map_get == NULL ||
            endpoint->backend.map_get(
                endpoint->backend.context, map_request.start + count, &entry) != ORPHEUS_OK) {
            break;
        }
        memcpy(payload_out + header_size + count * sizeof(entry), &entry, sizeof(entry));
    }
    page.count = (uint32_t)count;
    memcpy(payload_out, &page, sizeof(page));
    return bridge_response(
        request, ORPHEUS_MSG_FLAG_ERROR & 0u, payload_out,
        header_size + count * sizeof(OrpheusBridgeMapEntry),
        response, response_cap, response_len);
}

void orpheus_bridge_endpoint_init(
    OrpheusBridgeEndpoint* endpoint,
    const OrpheusBridgeBackend* backend,
    const OrpheusBridgeHello* hello,
    const OrpheusBridgeIdentity* identity) {
    if (endpoint == NULL) return;
    memset(endpoint, 0, sizeof(*endpoint));
    if (backend != NULL) endpoint->backend = *backend;
    if (hello != NULL) endpoint->hello = *hello;
    if (identity != NULL) endpoint->identity = *identity;
    endpoint->hello.protocol_version = ORPHEUS_BRIDGE_PROTOCOL_VERSION;
    endpoint->hello.abi_version = ORPHEUS_ABI_VERSION;
    if (endpoint->hello.max_message == 0u ||
        endpoint->hello.max_message > ORPHEUS_BRIDGE_MAX_MESSAGE) {
        endpoint->hello.max_message = ORPHEUS_BRIDGE_MAX_MESSAGE;
    }
    endpoint->hello.capabilities |= ORPHEUS_BRIDGE_CAP_HALF_DUPLEX;
    if (endpoint->backend.map_count != NULL && endpoint->backend.map_get != NULL) {
        endpoint->hello.capabilities |= ORPHEUS_BRIDGE_CAP_MAP;
        endpoint->identity.id_count = (uint32_t)endpoint->backend.map_count(
            endpoint->backend.context);
    }
    if (endpoint->backend.request_stop != NULL) {
        endpoint->hello.capabilities |= ORPHEUS_BRIDGE_CAP_STOP;
    }
    endpoint->hello.capabilities &= ~ORPHEUS_BRIDGE_CAP_BULK_FRAGMENT;
    if (endpoint->hello.max_message >= 32u) {
        endpoint->bulk = orpheus_bridge_bulk_create(
            endpoint->backend.context, endpoint->backend.bulk_target, endpoint->backend.now_ms);
        if (endpoint->bulk != NULL)
            endpoint->hello.capabilities |= ORPHEUS_BRIDGE_CAP_BULK_FRAGMENT;
    }
}

void orpheus_bridge_endpoint_destroy(OrpheusBridgeEndpoint* endpoint) {
    if (endpoint == NULL) return;
    orpheus_bridge_bulk_destroy(endpoint->bulk);
    endpoint->bulk = NULL;
}

int orpheus_bridge_endpoint_process(
    OrpheusBridgeEndpoint* endpoint,
    const uint8_t* request_bytes,
    size_t request_len,
    uint8_t* response,
    size_t response_cap,
    size_t* response_len) {
    if (endpoint == NULL || request_bytes == NULL || response == NULL ||
        response_len == NULL || request_len < sizeof(OrpheusMessageHeader)) {
        return ORPHEUS_ERR_INVALID_ARG;
    }
    *response_len = 0u;
    OrpheusMessageHeader request;
    memcpy(&request, request_bytes, sizeof(request));
    const size_t payload_len = (size_t)ORPHEUS_MSG_PAYLOAD_WORDS(&request) * 4u;
    if (ORPHEUS_MSG_TYPE(&request) != ORPHEUS_MSG_CALL ||
        request_len != sizeof(request) + payload_len) {
        endpoint->stats.errors++;
        return ORPHEUS_ERR_INVALID_ARG;
    }

    endpoint->stats.calls++;
    if (response_cap > ORPHEUS_BRIDGE_MAX_MESSAGE) response_cap = ORPHEUS_BRIDGE_MAX_MESSAGE;
    if (response_cap > endpoint->hello.max_message) response_cap = endpoint->hello.max_message;
    const uint8_t* payload = request_bytes + sizeof(request);
    int result = ORPHEUS_OK;
    const uint32_t flags = ORPHEUS_MSG_FLAGS(&request);
    if (request_len > endpoint->hello.max_message ||
        (flags != 0u && flags != ORPHEUS_MSG_FLAG_FRAGMENT)) {
        endpoint->stats.errors++;
        return bridge_response(&request, ORPHEUS_MSG_FLAG_ERROR, NULL, 0u,
            response, response_cap, response_len);
    }
    if (flags == ORPHEUS_MSG_FLAG_FRAGMENT) {
        result = orpheus_bridge_bulk_message(
            endpoint->bulk, &request, payload, payload_len,
            response, response_cap, response_len);
        OrpheusMessageHeader reply;
        if (result != ORPHEUS_OK) endpoint->stats.errors++;
        else if (*response_len >= sizeof(reply)) {
            memcpy(&reply, response, sizeof(reply));
            if (ORPHEUS_MSG_FLAGS(&reply) & ORPHEUS_MSG_FLAG_ERROR) endpoint->stats.errors++;
        }
        return result;
    }
    switch (request.route_id) {
        case ORPHEUS_BRIDGE_ROUTE_HELLO:
            orpheus_bridge_bulk_reset_session(endpoint->bulk);
            result = bridge_response(&request, 0u, &endpoint->hello,
                sizeof(endpoint->hello), response, response_cap, response_len);
            break;
        case ORPHEUS_BRIDGE_ROUTE_IDENTITY:
            result = bridge_response(&request, 0u, &endpoint->identity,
                sizeof(endpoint->identity), response, response_cap, response_len);
            break;
        case ORPHEUS_BRIDGE_ROUTE_STOP:
            endpoint->stop_requested = true;
            if (endpoint->backend.request_stop != NULL) {
                endpoint->backend.request_stop(endpoint->backend.context);
            }
            result = bridge_response(&request, 0u, NULL, 0u,
                response, response_cap, response_len);
            break;
        case ORPHEUS_BRIDGE_ROUTE_STATS:
            result = bridge_response(&request, 0u, &endpoint->stats,
                sizeof(endpoint->stats), response, response_cap, response_len);
            break;
        case ORPHEUS_BRIDGE_ROUTE_MAP:
            result = bridge_map_response(endpoint, &request, payload, payload_len,
                response, response_cap, response_len);
            break;
        default:
            if ((request.route_id & 0xFFFF0000u) == ORPHEUS_BRIDGE_SYSTEM_PREFIX ||
                endpoint->backend.dispatch == NULL) {
                result = bridge_response(&request, ORPHEUS_MSG_FLAG_ERROR, NULL, 0u,
                    response, response_cap, response_len);
            } else {
                if (payload_len != 0u &&
                    orpheus_bridge_bulk_write_locked(endpoint->bulk, request.route_id)) {
                    result = bridge_response(&request, ORPHEUS_MSG_FLAG_ERROR, NULL, 0u,
                        response, response_cap, response_len);
                } else {
                    result = endpoint->backend.dispatch(
                        endpoint->backend.context, request_bytes, request_len,
                        response, response_cap, response_len);
                }
            }
            break;
    }
    if (result != ORPHEUS_OK || *response_len == 0u) endpoint->stats.errors++;
    return result;
}

bool orpheus_bridge_endpoint_should_stop(const OrpheusBridgeEndpoint* endpoint) {
    return endpoint != NULL && endpoint->stop_requested;
}
