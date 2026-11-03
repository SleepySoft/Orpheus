#include "orpheus_bulk_guarded_slew.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while(0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();void* state=NULL;
    const char* ids[]={"channels","elements","attack","decay","threshold","slow","initial"};
    OrpheusValue values[7]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=2}};
    const float defaults[]={1,-2,2,0.25f,0};
    for (unsigned i=2;i<7;++i) {values[i].type=ORPHEUS_VALUE_FLOAT;values[i].value.f32=defaults[i-2];}
    OrpheusConfig config={.sample_rate=48000,.block_size=128,.param_count=7,.param_ids=ids,.param_values=values};
    CHECK(api->create(&state,&config)==ORPHEUS_OK && api->prepare(state,&config)==ORPHEUS_OK);
    BulkGuardedSlewState* s=(BulkGuardedSlewState*)state;
    float data[]={0.5f,-0.5f,4,-4},result[4],flag=1;
    OrpheusBuffer in={.data=data,.channels=2,.frame_count=2,.frame_capacity=2,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out=in;out.data=result;
    OrpheusBuffer bound={.data=&flag,.channels=1,.frame_count=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in,NULL};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,.frame_count=128};
    const float plain[]={0,0,1,-2},continued[]={0,0,2,-4},bounded[]={0.25f,-0.25f,1,-2};
    CHECK(api->process(state,&ctx)==ORPHEUS_OK && out.frame_count==2 && !memcmp(result,plain,16));
    CHECK(api->process(state,&ctx)==ORPHEUS_OK && !memcmp(result,continued,16));
    CHECK(api->reset(state)==ORPHEUS_OK);inputs[1]=&bound;
    CHECK(api->process(state,&ctx)==ORPHEUS_OK && !memcmp(result,bounded,16));
    GuardedSlewHistory before[4];memcpy(before,s->history,sizeof(before));
    float saved[4];memset(result,0x55,sizeof(result));memcpy(saved,result,sizeof(saved));out.frame_count=19;
    for (unsigned fault=0;fault<9;++fault) {
        out.data=result;out.channels=2;out.frame_capacity=2;out.format=ORPHEUS_FORMAT_F32;
        bound.data=&flag;bound.format=ORPHEUS_FORMAT_F32;in.frame_count=2;in.frame_capacity=2;
        if (fault==0) out.frame_capacity=1;
        if (fault==1) out.channels=1;
        if (fault==2) out.format=ORPHEUS_FORMAT_I32;
        if (fault==3) bound.format=ORPHEUS_FORMAT_I32;
        if (fault==4) bound.data=NULL;
        if (fault==5) in.frame_count=1;
        if (fault==6) in.frame_capacity=1;
        if (fault==7) out.data=data+1;
        if (fault==8) bound.data=result+3;
        CHECK(api->process(state,&ctx)==ORPHEUS_ERR_INVALID_ARG && out.frame_count==19 &&
            !memcmp(result,saved,16) && !memcmp(before,s->history,sizeof(before)));
    }
    out.data=result;bound.data=&flag;out.channels=2;out.frame_capacity=2;out.format=ORPHEUS_FORMAT_F32;
    bound.format=ORPHEUS_FORMAT_F32;in.frame_count=2;in.frame_capacity=2;ctx.frame_count=0;
    CHECK(api->process(state,&ctx)==ORPHEUS_OK && out.frame_count==19 && !memcmp(before,s->history,sizeof(before)));
    ctx.frame_count=128;inputs[1]=NULL;CHECK(api->reset(state)==ORPHEUS_OK);outputs[0]=NULL;
    CHECK(api->process(state,&ctx)==ORPHEUS_OK);outputs[0]=&out;
    CHECK(api->process(state,&ctx)==ORPHEUS_OK && !memcmp(result,continued,16));
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.5f};
    CHECK(api->set_parameter(state,"attack",&value)==ORPHEUS_OK && s->initialized);
    value.value.f32=10;CHECK(api->reset(state)==ORPHEUS_OK);
    CHECK(api->set_parameter(state,"initial",&value)==ORPHEUS_OK && !s->initialized);
    for (unsigned i=0;i<4;++i) data[i]=10;
    CHECK(api->process(state,&ctx)==ORPHEUS_OK);
    for (unsigned i=0;i<4;++i) CHECK(result[i]==10);
    value.value.f32=20;CHECK(api->set_parameter(state,"initial",&value)==ORPHEUS_OK);
    for (unsigned i=0;i<4;++i) data[i]=20;
    CHECK(api->process(state,&ctx)==ORPHEUS_OK);
    for (unsigned i=0;i<4;++i) CHECK(result[i]==10.5f);
    CHECK(api->reset(state)==ORPHEUS_OK && api->process(state,&ctx)==ORPHEUS_OK);
    for (unsigned i=0;i<4;++i) CHECK(result[i]==20);
    memcpy(before,s->history,sizeof(before));values[0].value.i32=129;
    CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(before,s->history,sizeof(before)));
    values[0].value.i32=2;value.value.f32=-1;
    CHECK(api->set_parameter(state,"attack",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=1;CHECK(api->set_parameter(state,"decay",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=NAN;CHECK(api->set_parameter(state,"initial",&value)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(!memcmp(before,s->history,sizeof(before)));
    value.value.f32=0;CHECK(api->set_parameter(state,"initial",&value)==ORPHEUS_OK && api->reset(state)==ORPHEUS_OK);
    data[0]=NAN;data[1]=INFINITY;data[2]=-INFINITY;data[3]=-0.0f;
    CHECK(api->process(state,&ctx)==ORPHEUS_OK && result[0]==-2 && result[1]==0.5f && result[2]==-2 && result[3]==0);
    CHECK(api->get_parameter(state,"attack",&value)==ORPHEUS_OK && value.type==ORPHEUS_VALUE_FLOAT && value.value.f32==0.5f);
    CHECK(api->destroy(state)==ORPHEUS_OK);
    BulkGuardedSlewState external;config=(OrpheusConfig){.state_block=&external};
    CHECK(api->create(&state,&config)==ORPHEUS_OK && state==&external && !external.owns_memory);
    CHECK(api->prepare(state,&config)==ORPHEUS_OK && api->destroy(state)==ORPHEUS_OK && !external.history);
    return 0;
}
