#include "orpheus_placeholder.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    { .id = "interface", .name = "占位接口", .type = ORPHEUS_VALUE_STRING,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .affects_signature = true },
    { .id = "mode", .name = "桩行为", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "passthrough" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED },
    { .id = "routes", .name = "输出路由", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "0" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.placeholder", "1.0.0", ORPHEUS_ABI_VERSION,
    NULL, 0, params, 3, sizeof(PlaceholderState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }

static int create(void** state, const OrpheusConfig* config) {
    if (!state || !config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    *state = config->state_block;
    return ORPHEUS_OK;
}

static int destroy(void* state) { (void)state; return ORPHEUS_OK; }
static int reset(void* state) { (void)state; return ORPHEUS_OK; }

static int set_param(void* state, const char* id, const OrpheusValue* value) {
    PlaceholderState* s = (PlaceholderState*)state;
    if (!s || !id || !value || value->type != ORPHEUS_VALUE_STRING || !value->value.str)
        return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(id, "interface") == 0) return ORPHEUS_OK; /* 编译器负责解析签名。 */
    if (strcmp(id, "mode") == 0) {
        if (strcmp(value->value.str, "silence") == 0) s->silence = 1;
        else if (strcmp(value->value.str, "passthrough") == 0) s->silence = 0;
        else return ORPHEUS_ERR_INVALID_ARG;
        return ORPHEUS_OK;
    }
    if (strcmp(id, "routes") == 0) {
        const char* cursor = value->value.str;
        int routes[32];
        uint32_t count = 0;
        while (isspace((unsigned char)*cursor)) ++cursor;
        while (*cursor) {
            char* end;
            long index;
            if (count == 32) return ORPHEUS_ERR_INVALID_ARG;
            errno = 0;
            index = strtol(cursor, &end, 10);
            if (end == cursor || errno || index < -1 || index >= 32)
                return ORPHEUS_ERR_INVALID_ARG;
            while (isspace((unsigned char)*end)) ++end;
            if (*end && *end != ',') return ORPHEUS_ERR_INVALID_ARG;
            routes[count++] = (int)index;
            if (!*end) break;
            cursor = end + 1;
            while (isspace((unsigned char)*cursor)) ++cursor;
            if (!*cursor) return ORPHEUS_ERR_INVALID_ARG;
        }
        memcpy(s->routes, routes, count * sizeof(int));
        s->route_count = count;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int prepare(void* state, const OrpheusConfig* config) {
    PlaceholderState* s = (PlaceholderState*)state;
    uint32_t i;
    if (!s || !config) return ORPHEUS_ERR_INVALID_ARG;
    memset(s, 0, sizeof(*s));
    s->route_count = 1;
    for (i = 0; i < config->param_count; ++i) {
        int result = set_param(state, config->param_ids[i], &config->param_values[i]);
        if (result != ORPHEUS_OK) return result;
    }
    /* stdout 可能承载二进制 Bridge 协议，生命周期日志只能写 stderr。 */
    fprintf(stderr, "LOG 占位模块：仅执行骨架桩行为，不代表算法实现或模型等价\n");
    return ORPHEUS_OK;
}

static int process(void* state, const OrpheusProcessContext* ctx) {
    PlaceholderState* s = (PlaceholderState*)state;
    uint32_t i;
    if (!s || !ctx || ctx->output_count > 32 || ctx->input_count > 32)
        return ORPHEUS_ERR_INVALID_ARG;
    if ((ctx->input_count && !ctx->inputs) || (ctx->output_count && !ctx->outputs))
        return ORPHEUS_ERR_INVALID_ARG;
    if (!s->silence && s->route_count != ctx->output_count)
        return ORPHEUS_ERR_INVALID_ARG;
    for (i = 0; i < ctx->output_count; ++i) {
        OrpheusBuffer* out = ctx->outputs[i];
        const OrpheusBuffer* in = NULL;
        size_t bytes;
        if (!out) continue;
        if (!out->data || out->format != ORPHEUS_FORMAT_F32 ||
            out->frame_capacity < ctx->frame_count)
            return ORPHEUS_ERR_INVALID_ARG;
        bytes = (size_t)ctx->frame_count * out->channels * sizeof(float);
        if (!s->silence && s->routes[i] >= 0) {
            uint32_t index = (uint32_t)s->routes[i];
            if (index >= ctx->input_count) return ORPHEUS_ERR_INVALID_ARG;
            in = ctx->inputs[index];
            if (!in || !in->data || in->channels != out->channels ||
                in->format != ORPHEUS_FORMAT_F32 || in->frame_count < ctx->frame_count ||
                in->interleaved != out->interleaved)
                return ORPHEUS_ERR_INVALID_ARG;
        }
        if (in) memmove(out->data, in->data, bytes);
        else memset(out->data, 0, bytes);
        out->frame_count = ctx->frame_count;
    }
    return ORPHEUS_OK;
}

static int get_param(void* state, const char* id, OrpheusValue* value) {
    (void)state; (void)id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL, NULL
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &iface; }
