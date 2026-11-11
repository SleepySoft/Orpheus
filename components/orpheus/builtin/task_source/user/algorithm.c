#include "component_state.h"
#include "component_api.h"

int orpheus_builtin_task_source_set_parameter(orpheus_builtin_task_source_State* s,
    const char* key, const OrpheusValue* value) {
    (void)s;
    (void)key;
    (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

int orpheus_builtin_task_source_algorithm(orpheus_builtin_task_source_State* s,
    orpheus_builtin_task_source_IO* io, const OrpheusProcessContext* ctx) {
    (void)s;
    return orpheus_builtin_task_source_emit_out(ctx, &io->out);
}
