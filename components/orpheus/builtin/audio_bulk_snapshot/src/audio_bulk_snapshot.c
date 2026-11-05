#include "orpheus_audio_bulk_snapshot.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[] = {
    {.id="channels",.name="通道数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=22},
     .min_i32=1,.max_i32=128,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="elements",.name="期望样本数（0跟随音频）",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},
     .min_i32=0,.max_i32=65536,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
};
static const OrpheusPort ports[] = {
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.audio_bulk_snapshot",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .params=params,.param_count=2,.ports=ports,.port_count=2,.state_size=sizeof(AudioBulkSnapshotState),.alignment=8,.realtime_safe=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    bool external=config && config->state_block;
    *state=external?config->state_block:calloc(1,sizeof(AudioBulkSnapshotState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(AudioBulkSnapshotState));((AudioBulkSnapshotState*)*state)->owns_memory=!external;return 0;
}
static int destroy(void* state) {if (state && ((AudioBulkSnapshotState*)state)->owns_memory) free(state);return 0;}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values))) return ORPHEUS_ERR_INVALID_ARG;
    int32_t n=22,frames=0;
    for (unsigned i=0;i<config->param_count;++i) {
        const char* k=config->param_ids[i];const OrpheusValue* v=config->param_values+i;
        if (!k || v->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (!strcmp(k,"channels") && v->value.i32>=1 && v->value.i32<=128) n=v->value.i32;
        else if (!strcmp(k,"elements") && v->value.i32>=0 && v->value.i32<=65536) frames=v->value.i32;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
    AudioBulkSnapshotState* s=state;s->channels=n;s->elements=frames;return 0;
}
static int reset(void* state) {return state?0:ORPHEUS_ERR_INVALID_ARG;}
static int process(void* state,const OrpheusProcessContext* c) {
    if (!state || !c || !c->inputs || !c->outputs || c->input_count<1 || c->output_count<1) return ORPHEUS_ERR_INVALID_ARG;
    if (!c->frame_count) return orpheus_process_next(c);
    AudioBulkSnapshotState* s=state;unsigned n=(unsigned)s->channels,frames=c->frame_count;
    if (s->elements && (unsigned)s->elements!=frames) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned i=0;i<2;++i) {
        const OrpheusBuffer* b=i?c->outputs[0]:c->inputs[0];
        if (i && !b) continue;
        if (!b || !b->data || b->channels!=n || b->format!=ORPHEUS_FORMAT_F32 || !b->interleaved ||
            b->frame_capacity<frames || (!i && b->frame_count!=frames)) return ORPHEUS_ERR_INVALID_ARG;
    }
    if (c->frame_count!=frames) return ORPHEUS_ERR_INVALID_ARG;
    if (c->outputs[0]) {memcpy(c->outputs[0]->data,c->inputs[0]->data,(size_t)n*frames*4);c->outputs[0]->frame_count=frames;}
    return orpheus_process_next(c);
}
static const OrpheusComponentInterface api = {
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,.process=process
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &api;}
