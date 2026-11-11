#include "component_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1; } } while(0)
typedef struct { unsigned count,calls; bool fail; OrpheusSlotInfo matrix; } Capture;
static OrpheusSlotId add(void* user, const OrpheusSlotInfo* slot) {
    Capture* capture=user;
    if(!strcmp(slot->key,"matrix")) capture->matrix=*slot;
    capture->count++;
    return capture->fail ? ORPHEUS_SLOT_ID_INVALID : 0;
}
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if(port!=1 || !delivery || !delivery->buffer) return ORPHEUS_ERR_INVALID_ARG;
    capture->calls++;
    return capture->fail ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK;
}
static int shape(const OrpheusComponentInterface* api, unsigned rows, unsigned cols, unsigned frames) {
    size_t input_bytes=(size_t)frames*cols*sizeof(float),output_bytes=(size_t)frames*rows*sizeof(float);
    float* input=malloc(input_bytes),*output=malloc(output_bytes);
    CHECK(input && output);
    for(size_t i=0;i<(size_t)frames*cols;i++) input[i]=(float)((int)(i%103)-51)*.03125f;
    const char* ids[]={"rows","cols","matrix"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=rows},
        {.type=ORPHEUS_VALUE_INT,.value.i32=cols},{.type=ORPHEUS_VALUE_STRING,.value.str=""}};
    orpheus_builtin_matrix_mul_State state;
    OrpheusConfig config={.sample_rate=48000,.block_size=frames,.param_count=3,.param_ids=ids,
        .param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    Capture capture={0};
    OrpheusRegistry registry={.ctx=&capture,.add=add};
    CHECK(api->register_slots(handle,&registry)==0 && capture.count==3);
    CHECK(capture.matrix.count==rows*cols && capture.matrix.type==ORPHEUS_VALUE_FLOAT &&
        capture.matrix.offset==offsetof(orpheus_builtin_matrix_mul_State,algorithm.matrix) &&
        (capture.matrix.flags & ORPHEUS_SLOT_DOUBLE_BUFFERED));
    OrpheusBuffer in={.data=input,.format=ORPHEUS_FORMAT_F32,.channels=cols,
        .frame_count=frames,.frame_capacity=frames,.interleaved=true};
    OrpheusBuffer out=in;out.channels=rows;out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=emit,.user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=frames,.continuation=&continuation};
    for(unsigned pass=0;pass<2;pass++) {
        capture.fail=pass!=0;
        CHECK(api->process(handle,&ctx)==(pass ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK));
        CHECK(capture.calls==pass+1);
        for(unsigned n=0;n<frames;n++) for(unsigned r=0;r<rows;r++) {
            float expected=r<cols ? input[(size_t)n*cols+r] : 0.0f;
            CHECK(!memcmp(output+(size_t)n*rows+r,&expected,sizeof(float)));
        }
    }
    CHECK(api->register_slots(handle,&registry)==ORPHEUS_ERR_PROCESSING);
    in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && capture.calls==2);
    in.frame_count=frames;out.data=input;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && capture.calls==2);
    out.data=state.algorithm.matrix;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && capture.calls==2);
    inputs[0]=NULL;outputs[0]=NULL;ctx.frame_count=0;
    CHECK(api->process(handle,&ctx)==0 && capture.calls==2);
    CHECK(api->destroy(handle)==0);
    free(input);free(output);
    return 0;
}

static int transactions(const OrpheusComponentInterface* api) {
    const char* ids[]={"rows","cols","matrix"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=1},
        {.type=ORPHEUS_VALUE_INT,.value.i32=4},
        {.type=ORPHEUS_VALUE_STRING,.value.str="1e10,1,-1e10,1"}};
    orpheus_builtin_matrix_mul_State state,saved;
    OrpheusConfig config={.block_size=1,.param_count=3,.param_ids=ids,.param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    const char* invalid[]={"x","1x","1,,2","1,","1, ","nan","inf","-inf","1e9999","1e39"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        saved=state;params[2].value.str=invalid[i];
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
        CHECK(!memcmp(&state,&saved,sizeof(state)));
    }
    char boundary[2051];
    for(unsigned i=0;i<1025;i++) { boundary[i*2]='1';boundary[i*2+1]=','; }
    boundary[2049]=0;params[2].value.str=boundary;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(!memcmp(&state,&saved,sizeof(state)));
    float input[]={1,2,1,3},output=0;
    OrpheusBuffer in={.data=input,.format=ORPHEUS_FORMAT_F32,.channels=4,
        .frame_count=1,.frame_capacity=1,.interleaved=true};
    OrpheusBuffer out=in;out.data=&output;out.channels=1;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=1};
    CHECK(api->process(handle,&ctx)==0 && output==3.0f);
    CHECK(api->reset(handle)==0 && api->process(handle,&ctx)==0 && output==1.0f);
    boundary[2047]=0;
    CHECK(api->prepare(handle,&config)==0 && state.algorithm.matrix[1023]==1.0f);
    params[2].value.str=" .25 \t -.5,\n1";
    CHECK(api->prepare(handle,&config)==0);
    CHECK(state.algorithm.matrix[0]==.25f && state.algorithm.matrix[1]==-.5f &&
        state.algorithm.matrix[2]==1 && state.algorithm.matrix[3]==0);
    CHECK(api->destroy(handle)==0);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(transactions(api)==0);
    CHECK(shape(api,1,7,1)==0);
    CHECK(shape(api,7,1,17)==0);
    CHECK(shape(api,32,32,65536)==0);
    return 0;
}
