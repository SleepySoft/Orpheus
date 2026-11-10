#include "component_state.h"
#include "component_api.h"

#include <string.h>

int orpheus_builtin_async_bridge_reset(orpheus_builtin_async_bridge_State* state) {
    state->level_frames = 0;
    state->underruns = 0;
    state->overruns = 0;
    return ORPHEUS_OK;
}

int orpheus_builtin_async_bridge_hook(orpheus_builtin_async_bridge_State* state,
    uint32_t id, uint32_t event, const OrpheusBlob* request, OrpheusBlob* response) {
    if (event != ORPHEUS_EVENT_BRIDGE_STATS) return ORPHEUS_HOOK_CONTINUE;
    if (id || response || !request || !request->data || request->len != sizeof(OrpheusTaskBridgeStats))
        return ORPHEUS_HOOK_ERROR;
    OrpheusTaskBridgeStats stats;
    memcpy(&stats, request->data, sizeof(stats));
    if (stats.level_frames < 0) return ORPHEUS_HOOK_ERROR;
    state->level_frames = stats.level_frames;
    state->underruns = stats.underruns;
    state->overruns = stats.overruns;
    return ORPHEUS_HOOK_HANDLED;
}

int orpheus_builtin_async_bridge_algorithm(orpheus_builtin_async_bridge_State* state,
    orpheus_builtin_async_bridge_IO* io, const OrpheusProcessContext* ctx) {
    (void)ctx;
    if (!io->in.buffer || !io->out.buffer ||
        io->in.buffer->channels != (uint32_t)state->channels ||
        io->out.buffer->channels != (uint32_t)state->channels)
        return ORPHEUS_ERR_INVALID_ARG;
    const uint32_t frames = io->in.buffer->frame_count < io->out.buffer->frame_capacity
        ? io->in.buffer->frame_count : io->out.buffer->frame_capacity;
    const size_t samples = (size_t)frames * (uint32_t)state->channels;
    if (samples) memcpy(io->out.samples, io->in.samples, samples * sizeof(float));
    if (frames < io->out.buffer->frame_capacity)
        memset(io->out.samples + samples, 0,
            (size_t)(io->out.buffer->frame_capacity - frames) * (uint32_t)state->channels * sizeof(float));
    io->out.buffer->frame_count = io->out.buffer->frame_capacity;
    return orpheus_builtin_async_bridge_emit_out(ctx, &io->out);
}
