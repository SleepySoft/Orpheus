#include "component_state.h"
#include "component_api.h"

int orpheus_builtin_trigger_divider_validate(const orpheus_builtin_trigger_divider_Parameters* p) {
    return p->phase < p->factor ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_builtin_trigger_divider_reset(orpheus_builtin_trigger_divider_State* s) {
    s->algorithm.counter=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_trigger_divider_algorithm(orpheus_builtin_trigger_divider_State* s,
    orpheus_builtin_trigger_divider_IO* io, const OrpheusProcessContext* ctx) {
    uint32_t phase=s->algorithm.counter;
    s->algorithm.counter=(phase+1)%(uint32_t)s->factor;
    return phase==(uint32_t)s->phase ? orpheus_builtin_trigger_divider_emit_out(ctx,&io->out) : ORPHEUS_OK;
}
