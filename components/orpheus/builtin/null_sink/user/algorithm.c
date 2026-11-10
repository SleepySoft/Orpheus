#include "component_state.h"
#include "component_api.h"

int orpheus_builtin_null_sink_algorithm(orpheus_builtin_null_sink_State* state,
    orpheus_builtin_null_sink_IO* io, const OrpheusProcessContext* ctx) {
    (void)state;
    (void)ctx;
    if (!io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
