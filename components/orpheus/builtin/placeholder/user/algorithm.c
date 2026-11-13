#include "component_state.h"
#include "component_api.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 解析 routes：逗号分隔的输入序号列表，-1 表示该输出置零；与编译器 validate_routes 同规则。 */
static int parse_routes(orpheus_builtin_placeholder_State* s, const char* text) {
    const char* cursor = text;
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
    memcpy(s->algorithm.routes, routes, count * sizeof(int));
    s->algorithm.route_count = count;
    return ORPHEUS_OK;
}

static int apply_mode(orpheus_builtin_placeholder_State* s, const char* mode) {
    if (!strcmp(mode, "silence")) s->algorithm.silence = 1;
    else if (!strcmp(mode, "passthrough")) s->algorithm.silence = 0;
    else return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}

int orpheus_builtin_placeholder_prepare(orpheus_builtin_placeholder_State* s,
    const OrpheusConfig* config) {
    (void)config;
    int result = apply_mode(s, s->mode);
    if (result != ORPHEUS_OK) return result;
    result = parse_routes(s, s->routes);
    if (result != ORPHEUS_OK) return result;
    /* stdout 可能承载二进制 Bridge 协议，生命周期日志只能写 stderr。 */
    fprintf(stderr, "LOG 占位模块：仅执行骨架桩行为，不代表算法实现或模型等价\n");
    return ORPHEUS_OK;
}

int orpheus_builtin_placeholder_reset(orpheus_builtin_placeholder_State* s) {
    /* 桩行为由配置决定，reset 保留路由/模式，不重新解析 */
    (void)s;
    return ORPHEUS_OK;
}

int orpheus_builtin_placeholder_set_parameter(orpheus_builtin_placeholder_State* s,
    const char* key, const OrpheusValue* value) {
    if (!strcmp(key, "interface")) return ORPHEUS_OK; /* 编译器负责解析签名，重启生效 */
    if (!strcmp(key, "mode")) return apply_mode(s, value->value.str);
    if (!strcmp(key, "routes")) return parse_routes(s, value->value.str);
    return ORPHEUS_ERR_NOT_FOUND;
}

int orpheus_builtin_placeholder_algorithm(orpheus_builtin_placeholder_State* s,
    orpheus_builtin_placeholder_IO* io, const OrpheusProcessContext* ctx) {
    if (io->output_count > 32 || io->input_count > 32)
        return ORPHEUS_ERR_INVALID_ARG;
    if ((io->input_count && !io->inputs) || (io->output_count && !io->outputs))
        return ORPHEUS_ERR_INVALID_ARG;
    if (!s->algorithm.silence && s->algorithm.route_count != io->output_count)
        return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t i = 0; i < io->output_count; ++i) {
        OrpheusBuffer* out = io->outputs[i];
        const OrpheusBuffer* in = NULL;
        if (!out) continue;
        if (!out->data || out->format != ORPHEUS_FORMAT_F32 ||
            out->frame_capacity < ctx->frame_count)
            return ORPHEUS_ERR_INVALID_ARG;
        size_t bytes = (size_t)ctx->frame_count * out->channels * sizeof(float);
        if (!s->algorithm.silence && s->algorithm.routes[i] >= 0) {
            uint32_t index = (uint32_t)s->algorithm.routes[i];
            if (index >= io->input_count) return ORPHEUS_ERR_INVALID_ARG;
            in = io->inputs[index];
            if (!in || !in->data || in->channels != out->channels ||
                in->format != ORPHEUS_FORMAT_F32 || in->frame_count < ctx->frame_count ||
                in->interleaved != out->interleaved)
                return ORPHEUS_ERR_INVALID_ARG;
        }
        /* 显式路由允许输出与输入共享缓冲（memmove），不视为别名错误 */
        if (in) memmove(out->data, in->data, bytes);
        else memset(out->data, 0, bytes);
        out->frame_count = ctx->frame_count;
    }
    return orpheus_process_next(ctx);
}
