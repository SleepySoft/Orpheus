#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ORPHEUS_TEST_GENERATED
#include "orpheus_graph.h"
#include "orpheus_control.h"
#define MESSAGE orpheus_control_message
#define REGISTER_HOOK orpheus_control_register_hook
#else
#include "orpheus_runtime/plan.h"
#include "orpheus_runtime/runtime.h"
static orpheus::Runtime runtime;
#define MESSAGE runtime.message
#define REGISTER_HOOK runtime.register_hook
#endif

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

static int oversized_hook(void* context, uint32_t id, uint32_t event,
                          const OrpheusBlob* request, OrpheusBlob* response) {
    (void)id; (void)event; (void)request;
    response->len = *(uint32_t*)context;
    return ORPHEUS_HOOK_HANDLED;
}

int main(int argc, char** argv) {
    uint32_t scalar_id;
    uint32_t bulk_id;
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc == 3);
    CHECK(orpheus_generated_init(48000, 32) == ORPHEUS_OK);
    scalar_id = (uint32_t)strtoul(argv[1], NULL, 0);
    bulk_id = (uint32_t)strtoul(argv[2], NULL, 0);
#else
    CHECK(argc == 5);
    orpheus::Plan plan = orpheus::Plan::load_from_file(argv[1]);
    CHECK(runtime.load_plan(plan, argv[2]) == ORPHEUS_OK);
    scalar_id = (uint32_t)strtoul(argv[3], NULL, 0);
    bulk_id = (uint32_t)strtoul(argv[4], NULL, 0);
#endif
    uint32_t hook_bytes = 0;
    const uint32_t hook_id = 0x40000001u;
    CHECK(REGISTER_HOOK(hook_id, oversized_hook, &hook_bytes) == ORPHEUS_OK);
    const size_t capacities[] = {0, 1, 7, 8, 11, 12, 4100, 65536};
    const uint32_t lengths[] = {4092, 4093, 4096, 0xFFFFFFFFu};
    uint8_t output[65536];
    for (size_t i = 0; i < sizeof(capacities) / sizeof(capacities[0]); ++i) {
        const size_t capacity = capacities[i];
        for (size_t j = 0; j < sizeof(lengths) / sizeof(lengths[0]); ++j) {
            hook_bytes = lengths[j];
            OrpheusMessageHeader request = {
                hook_id, ORPHEUS_MSG_MAKE(ORPHEUS_MSG_CALL, 0, 0xA55A, 0)
            };
            memset(output, 0xA5, sizeof(output));
            size_t length = 99;
            int rc = MESSAGE((const uint8_t*)&request, sizeof(request),
                             output, capacity, &length);
            if (capacity < sizeof(request)) {
                CHECK(rc != ORPHEUS_OK && length == 0);
            } else {
                CHECK(rc == ORPHEUS_OK);
                const OrpheusMessageHeader* response = (const OrpheusMessageHeader*)output;
                CHECK(ORPHEUS_MSG_CALL_ID(response) == 0xA55A);
                const bool error = hook_bytes > 4092 || hook_bytes > capacity - sizeof(request);
                CHECK(((ORPHEUS_MSG_FLAGS(response) & ORPHEUS_MSG_FLAG_ERROR) != 0u) == error);
                CHECK(length == sizeof(request) + (error ? 0u : hook_bytes));
                CHECK(length == sizeof(request) + ORPHEUS_MSG_PAYLOAD_WORDS(response) * 4u);
            }
            if (capacity < sizeof(output)) CHECK(output[capacity] == 0xA5);
        }
        OrpheusMessageHeader read = {
            scalar_id, ORPHEUS_MSG_MAKE(ORPHEUS_MSG_CALL, 0, 17, 0)
        };
        memset(output, 0xA5, sizeof(output));
        size_t length = 99;
        int rc = MESSAGE((const uint8_t*)&read, sizeof(read), output, capacity, &length);
        if (capacity < 8) CHECK(rc != ORPHEUS_OK && length == 0);
        else {
            CHECK(rc == ORPHEUS_OK && length == (capacity < 12 ? 8u : 12u));
            CHECK(((ORPHEUS_MSG_FLAGS((const OrpheusMessageHeader*)output) &
                    ORPHEUS_MSG_FLAG_ERROR) != 0u) == (capacity < 12));
        }
        if (capacity < sizeof(output)) CHECK(output[capacity] == 0xA5);
        read.route_id = bulk_id;
        memset(output, 0xA5, sizeof(output));
        length = 99;
        rc = MESSAGE((const uint8_t*)&read, sizeof(read), output, capacity, &length);
        if (capacity < 8) CHECK(rc != ORPHEUS_OK && length == 0);
        else {
            const OrpheusMessageHeader* response = (const OrpheusMessageHeader*)output;
            CHECK(rc == ORPHEUS_OK && length == 8);
            CHECK(ORPHEUS_MSG_FLAGS(response) & ORPHEUS_MSG_FLAG_ERROR);
            CHECK(ORPHEUS_MSG_CALL_ID(response) == 17);
            CHECK(ORPHEUS_MSG_PAYLOAD_WORDS(response) == 0);
        }
        if (capacity < sizeof(output)) CHECK(output[capacity] == 0xA5);
    }
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
