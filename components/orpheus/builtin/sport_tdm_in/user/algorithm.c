#include "component_state.h"
#include "component_api.h"
#include <limits.h>
#include <string.h>

int orpheus_builtin_sport_tdm_in_normalize(orpheus_builtin_sport_tdm_in_Parameters* p,
    const char* key) {
    return !key && (p->ready || p->underruns) ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}

int orpheus_builtin_sport_tdm_in_reset(orpheus_builtin_sport_tdm_in_State* s) {
    memset(s->algorithm.data, 0, sizeof(s->algorithm.data));
    s->ready = false;
    s->underruns = 0;
    return ORPHEUS_OK;
}

int orpheus_builtin_sport_tdm_in_prepare(orpheus_builtin_sport_tdm_in_State* s,
    const OrpheusConfig* config) {
    if (config->sample_rate != (double)s->sample_rate) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_sport_tdm_in_reset(s);
}

int orpheus_builtin_sport_tdm_in_set_parameter(orpheus_builtin_sport_tdm_in_State* s,
    const char* key, const OrpheusValue* value) {
    (void)s;
    (void)value;
    return !strcmp(key, "ready") ? ORPHEUS_OK : ORPHEUS_ERR_UNSUPPORTED;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return na && nb && (x >= y ? x - y < nb : y - x < na);
}

int orpheus_builtin_sport_tdm_in_algorithm(orpheus_builtin_sport_tdm_in_State* s,
    orpheus_builtin_sport_tdm_in_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->outputs || !ctx->output_count || ctx->sample_rate != (double)s->sample_rate)
        return ORPHEUS_ERR_INVALID_ARG;
    if (!ctx->frame_count) return ORPHEUS_OK;
    /* SPORT 触发按 Task 整块交付：不接受变长触发块 */
    if (ctx->frame_count != s->_orpheus_config_frames) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes = (size_t)ctx->frame_count * (uint32_t)s->channels * sizeof(float);
    if (io->out.buffer && overlap(io->out.samples, bytes, s, sizeof(*s)))
        return ORPHEUS_ERR_INVALID_ARG;
    if (!s->ready) {
        if (s->underruns < INT_MAX) ++s->underruns;
        return ORPHEUS_ERR_PROCESSING;
    }
    if (io->out.buffer) memcpy(io->out.samples, s->algorithm.data, bytes);
    s->ready = false;
    return orpheus_builtin_sport_tdm_in_emit_out(ctx, &io->out);
}
