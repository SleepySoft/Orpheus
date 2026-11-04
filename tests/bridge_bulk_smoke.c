#include "orpheus_bridge_endpoint.h"

#include <stdio.h>
#include <string.h>

#define N 2048u
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "分片测试失败：%s（行 %d）\n", #condition, __LINE__); return 1; \
} } while (0)

static int32_t integers[N], integer_shadow[N], expected_int[N];
static float floats[N], expected_float[N];
static uint32_t large[ORPHEUS_BULK_MAX_BYTES / 4u];
static uint8_t response[ORPHEUS_BRIDGE_MAX_MESSAGE];
static size_t response_len;
static uint64_t ticks = 100u;
static uint32_t pending = 1u, call_id;

static void clear_pending(void* context) { *(uint32_t*)context = 0u; }
static uint64_t clock_ms(void* context) { (void)context; return ticks; }

static int target(void* context, uint32_t id, OrpheusBridgeBulkTarget* output) {
    (void)context;
    memset(output, 0, sizeof(*output));
    output->count = N;
    output->writable = true;
    if (id == 1u || id == 4u) {
        output->active = integers;
        output->shadow = integer_shadow;
        output->task = "alpha";
        output->type = ORPHEUS_VALUE_INT;
        output->min_i32 = -1000000;
        output->max_i32 = 1000000;
        output->pending = &pending;
        output->clear_pending = clear_pending;
    } else if (id == 2u) {
        output->active = floats;
        output->task = "beta";
        output->type = ORPHEUS_VALUE_FLOAT;
    } else if (id == 3u) {
        output->active = large;
        output->task = "alpha";
        output->count = sizeof(large) / sizeof(large[0]);
        output->type = ORPHEUS_VALUE_INT;
    } else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}

static int dispatch(void* context, const uint8_t* input, size_t input_len,
                    uint8_t* output, size_t cap, size_t* length) {
    (void)context; (void)input_len;
    if (cap < 8u) return ORPHEUS_ERR_OUT_OF_MEMORY;
    OrpheusMessageHeader header;
    memcpy(&header, input, 8u);
    header.bits = ORPHEUS_MSG_MAKE(ORPHEUS_MSG_RESPONSE, 0u, ORPHEUS_MSG_CALL_ID(&header), 0u);
    memcpy(output, &header, 8u);
    *length = 8u;
    return ORPHEUS_OK;
}

static uint32_t fragment(OrpheusBridgeEndpoint* endpoint, uint32_t route, uint32_t operation,
                         uint32_t transfer, uint32_t offset, uint32_t count,
                         uint32_t total, const void* values) {
    uint8_t request[ORPHEUS_BRIDGE_MAX_MESSAGE];
    const size_t bytes = operation == ORPHEUS_BULK_WRITE ? count * 4u : 0u;
    if (bytes > sizeof(request) - 28u) return UINT32_MAX;
    OrpheusMessageHeader header = {route, ORPHEUS_MSG_MAKE(
        ORPHEUS_MSG_CALL, ORPHEUS_MSG_FLAG_FRAGMENT, ++call_id, (uint32_t)(5u + bytes / 4u))};
    OrpheusBulkFragment f = {operation, transfer, offset, count, total};
    memcpy(request, &header, 8u);
    memcpy(request + 8u, &f, 20u);
    if (bytes != 0u) memcpy(request + 28u, values, bytes);
    int result = orpheus_bridge_endpoint_process(endpoint, request, 28u + bytes,
        response, sizeof(response), &response_len);
    if (result != ORPHEUS_OK || response_len < 28u) return UINT32_MAX;
    OrpheusMessageHeader reply;
    OrpheusBulkFragment echoed;
    memcpy(&reply, response, 8u);
    memcpy(&echoed, response + 8u, 20u);
    if (reply.route_id != route || ORPHEUS_MSG_CALL_ID(&reply) != call_id ||
        ORPHEUS_MSG_TYPE(&reply) != ORPHEUS_MSG_RESPONSE ||
        !(ORPHEUS_MSG_FLAGS(&reply) & ORPHEUS_MSG_FLAG_FRAGMENT) ||
        response_len != 8u + ORPHEUS_MSG_PAYLOAD_WORDS(&reply) * 4u ||
        echoed.operation != operation || echoed.transfer_id != transfer ||
        echoed.offset != offset || echoed.count != count ||
        (echoed.total != total && total != 0u)) return UINT32_MAX;
    if (ORPHEUS_MSG_FLAGS(&reply) & ORPHEUS_MSG_FLAG_ERROR) {
        uint32_t error;
        if (response_len != 32u) return UINT32_MAX;
        memcpy(&error, response + 28u, 4u);
        return error;
    }
    return 0u;
}

int main(void) {
    OrpheusBridgeEndpoint endpoint;
    OrpheusBridgeBackend backend = {0};
    backend.bulk_target = target; backend.now_ms = clock_ms; backend.dispatch = dispatch;
    orpheus_bridge_endpoint_init(&endpoint, &backend, NULL, NULL);
    CHECK(endpoint.hello.capabilities & ORPHEUS_BRIDGE_CAP_BULK_FRAGMENT);
    for (uint32_t i = 0u; i < N; ++i) {
        integers[i] = 7; integer_shadow[i] = -1; floats[i] = 9.0f;
        expected_int[i] = (int32_t)i - 1024;
        expected_float[i] = (float)i * 0.125f;
    }

    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_WRITE, 100, 1018, 1018, N,
                   expected_int + 1018) == 0u);
    CHECK(fragment(&endpoint, 2, ORPHEUS_BULK_WRITE, 200, 0, 1018, N, expected_float) == 0u);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_COMMIT, 100, 0, 0, N, NULL) == ORPHEUS_BULK_INCOMPLETE);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_WRITE, 101, 0, 1, N, expected_int) == ORPHEUS_BULK_BUSY);
    CHECK(fragment(&endpoint, 4, ORPHEUS_BULK_WRITE, 102, 0, 1, N, expected_int) == ORPHEUS_BULK_BUSY);
    CHECK(fragment(&endpoint, 2, ORPHEUS_BULK_WRITE, 100, 0, 1, N, expected_int) == ORPHEUS_BULK_CONFLICT);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_READ, 100, 0, 1, N, NULL) == ORPHEUS_BULK_CONFLICT);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_WRITE, 100, 0, 1, N - 1, expected_int) == ORPHEUS_BULK_CONFLICT);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_WRITE, 100, 1018, 1018, N,
                   expected_int + 1018) == 0u);
    int32_t conflicting[] = {99, 99};
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_WRITE, 100, 1017, 2, N, conflicting) == ORPHEUS_BULK_CONFLICT);
    orpheus_bridge_bulk_service(endpoint.bulk, NULL);
    CHECK(integers[0] == 7 && integers[N - 1] == 7 && floats[0] == 9.0f);

    uint8_t ordinary[12] = {0};
    OrpheusMessageHeader ordinary_header = {1, ORPHEUS_MSG_MAKE(ORPHEUS_MSG_CALL, 0, 55, 1)};
    memcpy(ordinary, &ordinary_header, 8u);
    CHECK(orpheus_bridge_endpoint_process(&endpoint, ordinary, sizeof(ordinary),
          response, sizeof(response), &response_len) == ORPHEUS_OK);
    memcpy(&ordinary_header, response, 8u);
    CHECK(ORPHEUS_MSG_FLAGS(&ordinary_header) & ORPHEUS_MSG_FLAG_ERROR);

    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_WRITE, 100, 0, 1018, N, expected_int) == 0u);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_WRITE, 100, 2036, 12, N, expected_int + 2036) == 0u);
    CHECK(fragment(&endpoint, 2, ORPHEUS_BULK_WRITE, 200, 1018, 1018, N, expected_float + 1018) == 0u);
    CHECK(fragment(&endpoint, 2, ORPHEUS_BULK_WRITE, 200, 2036, 12, N, expected_float + 2036) == 0u);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_COMMIT, 100, 0, 0, N, NULL) == ORPHEUS_BULK_BUSY);
    CHECK(fragment(&endpoint, 2, ORPHEUS_BULK_COMMIT, 200, 0, 0, N, NULL) == ORPHEUS_BULK_BUSY);
    orpheus_bridge_bulk_service(endpoint.bulk, "wrong");
    CHECK(integers[0] == 7 && floats[0] == 9.0f);
    orpheus_bridge_bulk_service(endpoint.bulk, "alpha");
    CHECK(memcmp(integers, expected_int, sizeof(integers)) == 0);
    CHECK(memcmp(integer_shadow, expected_int, sizeof(integers)) == 0 && pending == 0u);
    CHECK(floats[0] == 9.0f);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_COMMIT, 100, 0, 0, N, NULL) == 0u);
    integers[0] = 12345;
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_COMMIT, 100, 0, 0, N, NULL) == 0u);
    CHECK(integers[0] == 12345);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_CANCEL, 100, 0, 0, N, NULL) == ORPHEUS_BULK_CLOSED);
    orpheus_bridge_bulk_service(endpoint.bulk, "beta");
    CHECK(memcmp(floats, expected_float, sizeof(floats)) == 0);
    CHECK(fragment(&endpoint, 2, ORPHEUS_BULK_COMMIT, 200, 0, 0, N, NULL) == 0u);

    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_READ, 300, 0, 0, 0, NULL) == ORPHEUS_BULK_BUSY);
    orpheus_bridge_bulk_service(endpoint.bulk, "alpha");
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_READ, 300, 0, 0, 0, NULL) == 0u);
    OrpheusBulkFragment opened;
    memcpy(&opened, response + 8u, 20u);
    CHECK(opened.total == N && response_len == 28u);
    for (uint32_t i = 0u; i < N; ++i) integers[i] = 77;
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_READ, 300, 1018, 1018, N, NULL) == 0u);
    CHECK(response_len == ORPHEUS_BRIDGE_MAX_MESSAGE);
    CHECK(memcmp(response + 28u, expected_int + 1018, 1018u * 4u) == 0);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_READ, 300, 0, 1, N, NULL) == 0u);
    int32_t first;
    memcpy(&first, response + 28u, 4u);
    CHECK(first == 12345);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_CANCEL, 300, 0, 0, 0, NULL) == 0u);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_CANCEL, 300, 0, 0, 0, NULL) == 0u);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_READ, 301, 0, 0, 0, NULL) == ORPHEUS_BULK_BUSY);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_CANCEL, 301, 0, 0, 0, NULL) == 0u);
    orpheus_bridge_bulk_service(endpoint.bulk, NULL);
    CHECK(integers[0] == 77);

    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_WRITE, 400, 0, 1, N, expected_int) == 0u);
    ticks += ORPHEUS_BULK_TIMEOUT_MS + 1u;
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_COMMIT, 400, 0, 0, N, NULL) == ORPHEUS_BULK_EXPIRED);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_WRITE, 401, 0, 1, N, expected_int) == 0u);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_CANCEL, 401, 0, 0, N, NULL) == 0u);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_WRITE, 402, 0, 1, 1, expected_int) == 0u);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_COMMIT, 402, 0, 0, 1, NULL) == ORPHEUS_BULK_BUSY);
    ticks += ORPHEUS_BULK_TIMEOUT_MS + 1u;
    orpheus_bridge_bulk_service(endpoint.bulk, "alpha");
    CHECK(integers[0] == 77);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_COMMIT, 402, 0, 0, 1, NULL) == ORPHEUS_BULK_EXPIRED);
    orpheus_bridge_bulk_reset_session(endpoint.bulk);
    for (uint32_t i = 0; i < ORPHEUS_BULK_MAX_TRANSFERS; ++i)
        CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_READ, 500 + i, 0, 0, 0, NULL) == ORPHEUS_BULK_BUSY);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_READ, 600, 0, 0, 0, NULL) == ORPHEUS_BULK_LIMIT);
    orpheus_bridge_bulk_reset_session(endpoint.bulk);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_COMMIT, 500, 0, 0, N, NULL) == ORPHEUS_BULK_UNKNOWN);

    CHECK(fragment(&endpoint, 3, ORPHEUS_BULK_READ, 700, 0, 0, 0, NULL) == ORPHEUS_BULK_BUSY);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_READ, 701, 0, 0, 0, NULL) == ORPHEUS_BULK_LIMIT);
    CHECK(fragment(&endpoint, 3, ORPHEUS_BULK_CANCEL, 700, 0, 0, 0, NULL) == 0u);
    CHECK(fragment(&endpoint, 3, ORPHEUS_BULK_WRITE, 702, 0, 1,
          (uint32_t)(sizeof(large) / 4u), expected_int) == ORPHEUS_BULK_LIMIT);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_READ, 800, N, 1, N, NULL) == ORPHEUS_BULK_INVALID);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_READ, 800, 0, 1019, N, NULL) == ORPHEUS_BULK_INVALID);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_READ, 0, 0, 1, N, NULL) == ORPHEUS_BULK_INVALID);
    CHECK(fragment(&endpoint, 99, ORPHEUS_BULK_READ, 800, 0, 1, N, NULL) == ORPHEUS_BULK_INVALID);
    int32_t invalid = INT32_MAX;
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_WRITE, 801, 0, 1, N, &invalid) == ORPHEUS_BULK_INVALID);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_CANCEL, 801, 0, 0, N, NULL) == 0u);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_WRITE, 802, 0, 1, 1, expected_int) == 0u);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_COMMIT, 802, 0, 0, 1, NULL) == ORPHEUS_BULK_BUSY);
    orpheus_bridge_bulk_service(endpoint.bulk, "alpha");
    ticks += ORPHEUS_BULK_TIMEOUT_MS + 1u;
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_COMMIT, 802, 0, 0, 1, NULL) == 0u);
    ticks += ORPHEUS_BULK_TIMEOUT_MS + 1u;
    integers[0] = 555;
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_COMMIT, 802, 0, 0, 1, NULL) == ORPHEUS_BULK_UNKNOWN);
    CHECK(integers[0] == 555);

    uint8_t malformed[28] = {0}, guarded[40];
    memset(guarded, 0xA5, sizeof(guarded));
    OrpheusMessageHeader malformed_header = {1, ORPHEUS_MSG_MAKE(
        ORPHEUS_MSG_CALL, ORPHEUS_MSG_FLAG_FRAGMENT, 99, 5)};
    OrpheusBulkFragment f = {ORPHEUS_BULK_READ, 900, 0, 0, 0};
    memcpy(malformed, &malformed_header, 8);
    memcpy(malformed + 8, &f, 20);
    CHECK(orpheus_bridge_endpoint_process(&endpoint, malformed, sizeof(malformed),
          guarded, 31, &response_len) == ORPHEUS_ERR_OUT_OF_MEMORY);
    CHECK(response_len == 0u);
    for (size_t i = 0; i < sizeof(guarded); ++i) CHECK(guarded[i] == 0xA5);
    CHECK(fragment(&endpoint, 1, ORPHEUS_BULK_COMMIT, 900, 0, 0, N, NULL) == ORPHEUS_BULK_UNKNOWN);
    malformed_header.bits = ORPHEUS_MSG_MAKE(ORPHEUS_MSG_CALL, ORPHEUS_MSG_FLAG_FRAGMENT, 100, 4);
    memcpy(malformed, &malformed_header, 8);
    CHECK(orpheus_bridge_endpoint_process(&endpoint, malformed, 24,
          response, sizeof(response), &response_len) == ORPHEUS_OK);
    CHECK(response_len == 32u);
    uint32_t error;
    memcpy(&error, response + 28, 4);
    CHECK(error == ORPHEUS_BULK_INVALID);
    orpheus_bridge_endpoint_destroy(&endpoint);
    return 0;
}
