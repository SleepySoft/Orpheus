#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int initialize_values(const char* text, uint32_t limit, float* values) {
    if (!text[0]) return ORPHEUS_OK;
    if (values) memset(values,0,2048*sizeof(float));
    uint32_t count=0;
    while (*text) {
        if (count>=limit) return ORPHEUS_ERR_INVALID_ARG;
        char* end;
        errno=0;
        float value=strtof(text,&end);
        if (end==text || errno==ERANGE || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        if (values) values[count]=value;
        count++;
        if (*end && *end!=',' && !isspace((unsigned char)*end)) return ORPHEUS_ERR_INVALID_ARG;
        text=end;
        while (*text==',' || isspace((unsigned char)*text)) ++text;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_bulk_relay_validate(const orpheus_builtin_bulk_relay_Parameters* p) {
    return initialize_values(p->initial_values,(uint32_t)p->count,NULL);
}

int orpheus_builtin_bulk_relay_prepare(orpheus_builtin_bulk_relay_State* s, const OrpheusConfig* config) {
    (void)config;
    return initialize_values(s->initial_values,(uint32_t)s->count,s->values);
}

int orpheus_builtin_bulk_relay_algorithm(orpheus_builtin_bulk_relay_State* s,
    orpheus_builtin_bulk_relay_IO* io, const OrpheusProcessContext* ctx) {
    (void)s;
    return orpheus_builtin_bulk_relay_emit_out(ctx,&io->out);
}
