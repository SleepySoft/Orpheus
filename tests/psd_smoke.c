#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
static unsigned calls;
static int fail;
static float seen_first,seen_last;
static OrpheusSlotInfo spectrum_slot;
static OrpheusSlotId next_slot=1;
static OrpheusSlotId add(void* user,const OrpheusSlotInfo* info) {
    (void)user;
    if(!strcmp(info->key,"spectrum")) spectrum_slot=*info;
    return next_slot++;
}
static int emit(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    orpheus_builtin_psd_State* s=user;
    if(port!=1 || !delivery || !delivery->buffer) return ORPHEUS_ERR_INVALID_ARG;
    seen_first=s->spectrum[0];seen_last=s->spectrum[1022];calls++;
    return fail;
}

static int shape(const OrpheusComponentInterface* api,unsigned frames,unsigned channels,unsigned smoothing) {
    size_t count=(size_t)frames*channels;
    float* input=calloc(count+channels,sizeof(float));float* output=malloc(count*sizeof(float));
    float* original=malloc(count*sizeof(float));CHECK(input && output && original);
    bool fft=frames==4 || frames==4096;
    for(unsigned f=0;f<frames;f++) for(unsigned c=0;c<channels;c++)
        input[f*channels+c]=frames==4 ? (f==1 ? (float)(c+1) : f==3 ? -(float)(c+1) : 0)
            : f==0 ? (float)(c+1) : 0;
    memcpy(original,input,count*sizeof(float));
    const char* ids[]={"channels","smoothing"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=channels},
        {.type=ORPHEUS_VALUE_INT,.value.i32=smoothing}};
    orpheus_builtin_psd_State state;void* handle=NULL;
    OrpheusConfig config={.sample_rate=48000,.block_size=frames,.state_block=&state,
        .param_ids=ids,.param_values=params,.param_count=2};
    CHECK(api->create(&handle,&config)==0 && handle==&state && api->prepare(handle,&config)==0);
    OrpheusRegistry registry={.add=add};
    CHECK(api->register_slots(handle,&registry)==0 && spectrum_slot.count==1023
        && spectrum_slot.size==sizeof(float) && spectrum_slot.kind==ORPHEUS_SLOT_PROBE
        && (spectrum_slot.flags&ORPHEUS_SLOT_READBACK) && !(spectrum_slot.flags&ORPHEUS_SLOT_DIRECT_WRITE));
    CHECK((char*)handle+spectrum_slot.offset==(char*)state.spectrum);
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1};
    CHECK(api->set_parameter(handle,"spectrum",&value)!=ORPHEUS_OK);
    OrpheusBuffer in={.data=input,.channels=channels,.frame_count=frames,.frame_capacity=frames,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out=in;out.data=output;out.frame_count=0;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=emit,.user=&state};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=frames,.continuation=&continuation};
    float smoothed=0,coefficient=1.0f/smoothing,magnitude=frames==4 ? 1.0f : 2.0f/frames;
    for(unsigned iteration=0;iteration<4;iteration++) {
        float saved[1023];memcpy(saved,state.spectrum,sizeof(saved));
        float* history=malloc(state.algorithm.magnitude_count*sizeof(float));CHECK(history);
        memcpy(history,state.algorithm.mags,state.algorithm.magnitude_count*sizeof(float));
        out.frame_capacity=frames-1;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        out.frame_capacity=frames;
        in.frame_count=frames-1;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG
            && !memcmp(saved,state.spectrum,sizeof(saved))
            && !memcmp(history,state.algorithm.mags,state.algorithm.magnitude_count*sizeof(float)));
        in.frame_count=frames;
        ctx.frame_count=0;calls=0;
        CHECK(api->process(handle,&ctx)==0 && !calls && !memcmp(saved,state.spectrum,sizeof(saved))
            && !memcmp(history,state.algorithm.mags,state.algorithm.magnitude_count*sizeof(float)));
        free(history);ctx.frame_count=frames;
        memcpy(input,original,count*sizeof(float));
        out.data=iteration==1 ? input : iteration==2 ? input+channels : output;
        if(fft) smoothed+=coefficient*(magnitude-smoothed);
        CHECK(api->process(handle,&ctx)==0 && calls==1 && out.frame_count==frames
            && !memcmp(out.data,original,count*sizeof(float)));
        for(unsigned i=0;i<1023;i++) {
            double expected=fft ? frames==4 ? (double)smoothed*i/1022 : smoothed : 0;
            CHECK(fabs(state.spectrum[i]-expected)<0.000002);
        }
        CHECK(fabsf(seen_first-(fft && frames!=4 ? smoothed : 0))<0.000002f
            && fabsf(seen_last-(fft ? smoothed : 0))<0.000002f);
    }
    out.data=output;
    if(frames>1) {
        float saved[1023];memcpy(saved,state.spectrum,sizeof(saved));
        float* history=malloc(state.algorithm.magnitude_count*sizeof(float));CHECK(history);
        memcpy(history,state.algorithm.mags,state.algorithm.magnitude_count*sizeof(float));
        for(unsigned iteration=0;iteration<2;iteration++) {
            unsigned short_frames=iteration ? frames-1 : frames/2;
            memcpy(input,original,count*sizeof(float));
            in.frame_count=ctx.frame_count=short_frames;calls=0;
            CHECK(api->process(handle,&ctx)==0 && calls==1 && out.frame_count==short_frames
                && !memcmp(output,original,(size_t)short_frames*channels*sizeof(float))
                && !memcmp(saved,state.spectrum,sizeof(saved))
                && !memcmp(history,state.algorithm.mags,state.algorithm.magnitude_count*sizeof(float)));
        }
        free(history);in.frame_count=ctx.frame_count=frames;
    }
    if(fft) smoothed+=coefficient*(magnitude-smoothed);
    memcpy(input,original,count*sizeof(float));fail=-77;calls=0;
    CHECK(api->process(handle,&ctx)==-77 && calls==1);fail=0;
    CHECK(fabsf(seen_last-(fft ? smoothed : 0))<0.000002f);
    float* old=state.algorithm.mags;params[1].value.i32=0;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.mags==old
        && state.smoothing==(int32_t)smoothing);
    params[1].value.i32=smoothing;
    CHECK(api->reset(handle)==0);
    for(unsigned i=0;i<1023;i++) CHECK(state.spectrum[i]==0);
    for(size_t i=0;i<state.algorithm.magnitude_count;i++) CHECK(state.algorithm.mags[i]==0);
    CHECK(api->prepare(handle,&config)==0 && api->destroy(handle)==0 && !state.algorithm.mags
        && !state.algorithm.re && !state.algorithm.im);
    config.state_block=NULL;CHECK(api->create(&handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    free(input);free(output);free(original);
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(shape(api,4,2,4)==0);
    CHECK(shape(api,4096,32,128)==0);
    CHECK(shape(api,6,3,8)==0);
    CHECK(shape(api,1,1,1)==0);
    CHECK(shape(api,2,1,1)==0);
    return 0;
}
