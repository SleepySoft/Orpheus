#ifndef ORPHEUS_CHAIN_H
#define ORPHEUS_CHAIN_H

#include "orpheus_abi.h"

typedef struct {
    uint64_t counter;
    uint32_t frame_count;
    uint32_t timeline_flags;
    uint64_t epoch;
    const void* trigger;
    bool task_mode;
    const OrpheusDelivery* delivery;
    uint32_t input_port;
} OrpheusChainRun;

typedef struct OrpheusChainNode OrpheusChainNode;
typedef struct {
    OrpheusChainNode* target;
    uint32_t every;
    uint32_t phase;
    uint64_t calls;
    uint32_t output_port;
    uint32_t input_port;
} OrpheusChainLink;

struct OrpheusChainNode {
    void* user;
    int (*invoke)(OrpheusChainNode*, const OrpheusChainRun*);
    int (*publish)(OrpheusChainNode*, const OrpheusChainRun*);
    OrpheusChainLink* children;
    uint32_t child_count;
    uint32_t period;
    uint32_t phase;
    uint64_t calls;
    int (*publish_outlet)(OrpheusChainNode*, const OrpheusChainRun*,
                          uint32_t, const OrpheusDelivery*);
};

typedef struct {
    OrpheusChainNode* node;
    const OrpheusChainRun* run;
} OrpheusChainCall;

static inline bool orpheus_chain_due(const OrpheusChainNode* node,
                                     const OrpheusChainRun* run) {
    return node->period > 0 && run->counter % node->period == node->phase;
}

static inline int orpheus_chain_enter(OrpheusChainNode* node, const OrpheusChainRun* run) {
    node->calls++;
    return node->invoke(node, run);
}

/* 发布缓冲必须先于下游调用，不能等整条递归链返回后再提交。 */
static inline int orpheus_chain_continue(void* user, uint32_t branch) {
    OrpheusChainCall* call = (OrpheusChainCall*)user;
    OrpheusChainNode* node = call->node;
    uint32_t i;
    int result;
    if (branch != UINT32_MAX && branch >= node->child_count)
        return ORPHEUS_ERR_INVALID_ARG;
    result = node->publish ? node->publish(node, call->run) : ORPHEUS_OK;
    if (result != ORPHEUS_OK) return result;
    for (i = 0; i < node->child_count; ++i) {
        if (branch != UINT32_MAX && branch != i) continue;
        OrpheusChainLink* link = &node->children[i];
        if (link->calls++ % link->every != link->phase) continue;
        result = orpheus_chain_enter(link->target, call->run);
        if (result != ORPHEUS_OK) return result;
    }
    return ORPHEUS_OK;
}

static inline int orpheus_chain_emit(void* user, uint32_t output_port,
                                     const OrpheusDelivery* delivery) {
    OrpheusChainCall* call = (OrpheusChainCall*)user;
    OrpheusChainNode* node = call->node;
    uint32_t i;
    int result;
    if (node->publish_outlet) {
        result = node->publish_outlet(node, call->run, output_port, delivery);
        if (result != ORPHEUS_OK) return result;
    }
    for (i = 0; i < node->child_count; ++i) {
        OrpheusChainLink* link = &node->children[i];
        OrpheusChainRun child_run;
        if (link->output_port != output_port) continue;
        child_run = *call->run;
        child_run.delivery = delivery;
        child_run.input_port = link->input_port;
        result = orpheus_chain_enter(link->target, &child_run);
        if (result != ORPHEUS_OK) return result;
    }
    return ORPHEUS_OK;
}

#endif
