#include "component_state.h"
#include "component_api.h"

#include <stdlib.h>
#include <string.h>

int orpheus_builtin_rate_sync_prepare(orpheus_builtin_rate_sync_State* s,
    const OrpheusConfig* config) {
    int configured_channels = 0;
    for (uint32_t i = 0; i < config->param_count; ++i)
        if (config->param_ids[i] && !strcmp(config->param_ids[i], "channels"))
            configured_channels = 1;
    if (!configured_channels && config->channels)
        s->channels = (int32_t)config->channels;
    if (s->channels < 1 || s->channels > 32)
        return ORPHEUS_ERR_INVALID_ARG;

    /* 对齐窗口：自动=合流块长；固定=buffer_length（0 回退自动）。
       显式窗口小于合流块长会丢数据，准备期拒绝，不留到运行期。 */
    const uint32_t block = config->block_size > 0 ? config->block_size : 1u;
    uint32_t align = s->mode == 1 && s->buffer_length > 0 ? (uint32_t)s->buffer_length : block;
    if (s->mode == 1 && s->buffer_length > 0 && align < block)
        return ORPHEUS_ERR_INVALID_ARG;
    if (align < 1) align = 1;

    /* 容量 = 对齐窗口 + 一个合流块：调用间余量最多 align-1 帧，加上本次整块不溢出 */
    const uint32_t capacity = align + block;
    for (int i = 0; i < 2; ++i) {
        s->algorithm.fifo[i] = calloc((size_t)s->channels * capacity, sizeof(float));
        if (!s->algorithm.fifo[i]) {
            free(s->algorithm.fifo[0]); free(s->algorithm.fifo[1]);
            s->algorithm.fifo[0] = s->algorithm.fifo[1] = NULL;
            return ORPHEUS_ERR_OUT_OF_MEMORY;
        }
        s->algorithm.wpos[i] = s->algorithm.rpos[i] = s->algorithm.stored[i] = 0;
    }
    s->algorithm.align = align;
    s->algorithm.capacity = capacity;
    return ORPHEUS_OK;
}

int orpheus_builtin_rate_sync_reset(orpheus_builtin_rate_sync_State* s) {
    for (int i = 0; i < 2; ++i) {
        if (s->algorithm.fifo[i])
            memset(s->algorithm.fifo[i], 0,
                (size_t)s->channels * s->algorithm.capacity * sizeof(float));
        s->algorithm.wpos[i] = s->algorithm.rpos[i] = s->algorithm.stored[i] = 0;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_rate_sync_destroy(orpheus_builtin_rate_sync_State* s) {
    free(s->algorithm.fifo[0]); free(s->algorithm.fifo[1]);
    s->algorithm.fifo[0] = s->algorithm.fifo[1] = NULL;
    return ORPHEUS_OK;
}

int orpheus_builtin_rate_sync_set_parameter(orpheus_builtin_rate_sync_State* s,
    const char* key, const OrpheusValue* value) {
    /* 全部参数 restart_required：FIFO 尺寸随签名变化，运行期改参由宿主重启实例 */
    (void)s; (void)key; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return na && nb && (x >= y ? x - y < nb : y - x < na);
}

int orpheus_builtin_rate_sync_algorithm(orpheus_builtin_rate_sync_State* s,
    orpheus_builtin_rate_sync_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!s->algorithm.fifo[0] || !s->algorithm.fifo[1]) return ORPHEUS_ERR_PROCESSING;
    const uint32_t ch = (uint32_t)s->channels;
    const uint32_t align = s->algorithm.align;
    const uint32_t cap = s->algorithm.capacity;
    const uint32_t frames = ctx->frame_count;
    if (!io->in0.buffer || !io->in1.buffer || !io->out.buffer ||
        io->in0.buffer->channels != ch || io->in1.buffer->channels != ch ||
        io->out.buffer->channels != ch ||
        io->in0.buffer->frame_count < frames || io->in1.buffer->frame_count < frames)
        return ORPHEUS_ERR_INVALID_ARG;
    if (frames > cap) return ORPHEUS_ERR_INVALID_ARG;
    const size_t in_bytes = (size_t)frames * ch * sizeof(float);
    const size_t fifo_bytes = (size_t)cap * ch * sizeof(float);
    const size_t out_bytes = (size_t)align * ch * sizeof(float);
    const float* inputs[] = {io->in0.samples, io->in1.samples};
    for (int port = 0; port < 2; ++port) {
        if (overlap(inputs[port], in_bytes, s, sizeof(*s)) ||
            overlap(inputs[port], in_bytes, s->algorithm.fifo[0], fifo_bytes) ||
            overlap(inputs[port], in_bytes, s->algorithm.fifo[1], fifo_bytes) ||
            overlap(inputs[port], in_bytes, io->out.samples, out_bytes))
            return ORPHEUS_ERR_INVALID_ARG;
    }
    if (overlap(io->out.samples, out_bytes, s, sizeof(*s)) ||
        overlap(io->out.samples, out_bytes, s->algorithm.fifo[0], fifo_bytes) ||
        overlap(io->out.samples, out_bytes, s->algorithm.fifo[1], fifo_bytes))
        return ORPHEUS_ERR_INVALID_ARG;

    /* 溢出前置拒绝：任何一路装不下本次整块就报错，不部分写入、不回滚状态 */
    for (int port = 0; port < 2; ++port)
        if (s->algorithm.stored[port] + frames > cap)
            return ORPHEUS_ERR_PROCESSING;

    /* 追加：写游标取模容量，帧布局与输入一致（交错 ch 通道） */
    for (int port = 0; port < 2; ++port) {
        float* fifo = s->algorithm.fifo[port];
        for (uint32_t f = 0; f < frames; ++f) {
            float* dst = fifo + (size_t)(s->algorithm.wpos[port] % cap) * ch;
            memcpy(dst, inputs[port] + (size_t)f * ch, ch * sizeof(float));
            s->algorithm.wpos[port]++;
        }
        s->algorithm.stored[port] += frames;
    }

    if (s->algorithm.stored[0] >= align && s->algorithm.stored[1] >= align) {
        const uint32_t nread = io->out.buffer->frame_capacity < align
            ? io->out.buffer->frame_capacity : align;
        float* out = io->out.samples;
        for (uint32_t f = 0; f < nread; ++f) {
            const float* a = s->algorithm.fifo[0] + (size_t)(s->algorithm.rpos[0] % cap) * ch;
            const float* b = s->algorithm.fifo[1] + (size_t)(s->algorithm.rpos[1] % cap) * ch;
            for (uint32_t c = 0; c < ch; ++c) out[(size_t)f * ch + c] = a[c] + b[c];
            s->algorithm.rpos[0]++;
            s->algorithm.rpos[1]++;
        }
        s->algorithm.stored[0] -= nread;
        s->algorithm.stored[1] -= nread;
        io->out.buffer->frame_count = nread;
    } else {
        io->out.buffer->frame_count = 0;
    }
    return orpheus_builtin_rate_sync_emit_out(ctx, &io->out);
}
