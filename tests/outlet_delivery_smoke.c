#include "orpheus_chain.h"

#include <stdio.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "line %d\n", __LINE__); return 1; } } while (0)

typedef struct {
    char name;
    int calls;
    int fail;
    uint32_t last_input;
    float last_value;
    uint64_t last_frame;
} State;

static char trace[128];
static size_t trace_size;
static uint32_t publications[16];
static size_t publication_count;

static void record(char value) {
    trace[trace_size++] = value;
    trace[trace_size] = '\0';
}

static int invoke(OrpheusChainNode* node, const OrpheusChainRun* run) {
    State* state = (State*)node->user;
    OrpheusChainCall call = {node, run};
    OrpheusContinuation continuation = {
        &call, orpheus_chain_continue, node->child_count, orpheus_chain_emit
    };
    OrpheusProcessContext ctx = {0};
    int result;
    state->calls++;
    state->last_input = run->input_port;
    if (run->delivery) {
        state->last_value = ((const float*)run->delivery->buffer->data)[0];
        state->last_frame = run->delivery->frame_index;
    }
    record(state->name);
    if (state->fail) return ORPHEUS_ERR_UNSUPPORTED;
    ctx.continuation = &continuation;
    if (node->child_count) {
        result = orpheus_emit(&ctx, 7, run->delivery);
        if (result != ORPHEUS_OK) return result;
    }
    record((char)(state->name - 'a' + 'A'));
    return ORPHEUS_OK;
}

static int publish(OrpheusChainNode* node, const OrpheusChainRun* run,
                   uint32_t output_port, const OrpheusDelivery* delivery) {
    (void)node;
    (void)run;
    (void)delivery;
    publications[publication_count++] = output_port;
    return ORPHEUS_OK;
}

int main(void) {
    State bs = {'b', 0}, cs = {'c', 0}, ds = {'d', 0};
    OrpheusChainNode b = {0}, c = {0}, d = {0}, a = {0};
    OrpheusChainLink b_links[] = {{&d, 1, 0, 0, 7, 0}};
    OrpheusChainLink c_links[] = {{&d, 1, 0, 0, 7, 0}};
    OrpheusChainLink a_links[] = {
        {&b, 1, 0, 0, 2, 0}, {&c, 1, 0, 0, 2, 0}, {&d, 1, 0, 0, 1, 1}
    };
    OrpheusChainRun run = {0};
    OrpheusChainCall call = {&a, &run};
    OrpheusContinuation continuation = {&call, orpheus_chain_continue, 3, orpheus_chain_emit};
    OrpheusProcessContext ctx = {0};
    float x = 1.0f, y = 2.0f;
    OrpheusBuffer xb = {&x, ORPHEUS_FORMAT_F32, 1, 1, 1, true};
    OrpheusBuffer yb = {&y, ORPHEUS_FORMAT_F32, 1, 1, 1, true};
    OrpheusDelivery xd = {&xb, 11, 3, ORPHEUS_TIMELINE_DISCONTINUITY};
    OrpheusDelivery yd = {&yb, 22, 3, 0};
    b.user = &bs; b.invoke = invoke; b.children = b_links; b.child_count = 1;
    c.user = &cs; c.invoke = invoke; c.children = c_links; c.child_count = 1;
    d.user = &ds; d.invoke = invoke;
    a.children = a_links; a.child_count = 3; a.publish_outlet = publish;
    ctx.continuation = &continuation;
    CHECK(orpheus_emit(&ctx, 2, &yd) == ORPHEUS_OK);
    CHECK(strcmp(trace, "bdDBcdDC") == 0);
    CHECK(bs.last_value == 2.0f && cs.last_value == 2.0f && ds.calls == 2);
    CHECK(publication_count == 1 && publications[0] == 2);
    record('x');
    CHECK(orpheus_emit(&ctx, 1, &xd) == ORPHEUS_OK);
    CHECK(strcmp(trace, "bdDBcdDCxdD") == 0);
    CHECK(ds.calls == 3 && ds.last_input == 1 && ds.last_value == 1.0f && ds.last_frame == 11);
    CHECK(publication_count == 2 && publications[1] == 1);
    trace_size = 0; trace[0] = '\0'; bs.fail = 1;
    CHECK(orpheus_emit(&ctx, 2, &yd) == ORPHEUS_ERR_UNSUPPORTED);
    CHECK(strcmp(trace, "b") == 0 && cs.calls == 1);
    bs.fail = 0;
    CHECK(orpheus_emit(&ctx, 2, &yd) == ORPHEUS_OK);
    CHECK(cs.calls == 2 && ds.calls == 5);
    CHECK(orpheus_emit(&ctx, 0, &xd) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(orpheus_emit(&ctx, 1, NULL) == ORPHEUS_ERR_INVALID_ARG);
    xb.frame_count = 2;
    CHECK(orpheus_emit(&ctx, 1, &xd) == ORPHEUS_ERR_INVALID_ARG);
    xb.frame_count = 1;
    ctx.continuation = NULL;
    CHECK(orpheus_emit(&ctx, 1, &xd) == ORPHEUS_OK);
    return 0;
}
