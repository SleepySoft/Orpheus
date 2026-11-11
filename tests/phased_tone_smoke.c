#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
typedef orpheus_builtin_indexed_tone_State IndexedToneState;
static int slots_seen;
static int fail_delivery(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    unsigned* calls=(unsigned*)user;
    ++*calls;
    return port==1 && delivery && delivery->buffer && delivery->buffer->frame_count==3 ?
        ORPHEUS_ERR_PROCESSING : ORPHEUS_ERR_INVALID_ARG;
}
static OrpheusSlotId check_slot(void* ctx, const OrpheusSlotInfo* slot) {
    (void)ctx;
    if (slot->kind==ORPHEUS_SLOT_SETTING && slot->count==1) ++slots_seen;
    else if (slot->kind==ORPHEUS_SLOT_BULK && slot->count==5 &&
             slot->type==ORPHEUS_VALUE_FLOAT && (slot->flags & ORPHEUS_SLOT_DOUBLE_BUFFERED)) ++slots_seen;
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* iface=orpheus_get_interface();
    const char* ids[]={"channels","execution_mode","filter_order","boost_coefs","cut_coefs",
                      "ramp0_current_gain","ramp0_target_gain","ramp0_coefficient","ramp0_remaining_blocks",
                      "ramp1_current_gain","ramp1_target_gain","ramp1_coefficient","ramp1_remaining_blocks"};
    OrpheusValue values[]={
        {.type=ORPHEUS_VALUE_INT,.value.i32=2}, {.type=ORPHEUS_VALUE_INT,.value.i32=1},
        {.type=ORPHEUS_VALUE_INT,.value.i32=1},
        {.type=ORPHEUS_VALUE_STRING,.value.str="0.5,0.25,0,0.5,0"},
        {.type=ORPHEUS_VALUE_STRING,.value.str="0.25,0.125,0,-0.25,0"},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.9f}, {.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.5f},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.1f}, {.type=ORPHEUS_VALUE_INT,.value.i32=1},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.25f}, {.type=ORPHEUS_VALUE_FLOAT,.value.f32=2},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.05f}, {.type=ORPHEUS_VALUE_INT,.value.i32=1}
    };
    OrpheusConfig config={.sample_rate=48000,.block_size=3,.param_count=13,.param_ids=ids,.param_values=values};
    IndexedToneState state={0};
    config.state_block=&state;
    void* instance=NULL;
    CHECK(iface->create(&instance,&config)==ORPHEUS_OK && instance==&state);
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK);
    OrpheusRegistry registry={.add=check_slot};
    CHECK(iface->register_slots(&state,&registry)==ORPHEUS_OK && slots_seen==22);
    float input[]={1,-1,0.5f,-0.5f,0.25f,-0.25f}, output[6], z[2]={0};
    OrpheusBuffer in={.data=input,.channels=2,.format=ORPHEUS_FORMAT_F32,.frame_count=3,.frame_capacity=3,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=2,.format=ORPHEUS_FORMAT_F32,.frame_capacity=3,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=3};
    float gain=0.9f;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK);
    for (unsigned n=0;n<3;++n) {
        gain*=1.1f;
        for (unsigned c=0;c<2;++c) {
            float x=input[n*2+c], filtered=x*0.25f+z[c];
            z[c]=x*0.125f+-0.25f*filtered;
            float expected=(gain-1)*filtered+x;
            CHECK(!memcmp(&output[n*2+c],&expected,4));
        }
    }
    CHECK(state.phase==1 && state.ramp0_remaining_blocks==0 && state.ramp0_current_gain==gain);
    context.frame_count=0;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.phase==1);
    context.frame_count=3;
    OrpheusValue freeze={.type=ORPHEUS_VALUE_BOOL,.value.b=true};
    CHECK(iface->set_parameter(&state,"freeze",&freeze)==ORPHEUS_OK);
    float previous_z=state.algorithm.z1[0];
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.phase==1 && state.algorithm.z1[0]!=previous_z);
    freeze.value.b=false;
    CHECK(iface->set_parameter(&state,"freeze",&freeze)==ORPHEUS_OK);
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.phase==2 && state.ramp1_remaining_blocks==0);
    CHECK(state.ramp1_current_gain!=2);
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.ramp1_current_gain==2);
    CHECK(iface->reset(&state)==ORPHEUS_OK && state.phase==2 && state.ramp1_current_gain==2 && state.algorithm.z1[0]==0);
    OrpheusValue request={.type=ORPHEUS_VALUE_FLOAT,.value.f32=128};
    CHECK(iface->set_parameter(&state,"gain_index",&request)==ORPHEUS_ERR_UNSUPPORTED);
    request.value.f32=128;
    CHECK(iface->set_parameter(&state,"ramp_ms",&request)==ORPHEUS_ERR_UNSUPPORTED);
    CHECK(iface->set_parameter(&state,"ramp_db_per_s",&request)==ORPHEUS_ERR_UNSUPPORTED);
    context.input_count=0;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);
    context.input_count=1;
    OrpheusValue invalid={.type=ORPHEUS_VALUE_FLOAT,.value.f32=NAN};
    CHECK(iface->set_parameter(&state,"ramp0_coefficient",&invalid)==ORPHEUS_ERR_INVALID_ARG);
    invalid=(OrpheusValue){.type=ORPHEUS_VALUE_INT,.value.i32=3};
    CHECK(iface->set_parameter(&state,"phase",&invalid)==ORPHEUS_ERR_INVALID_ARG);
    const char* bad[]={"1,2,3","1,2,3,4,5,6","1,2,3,4,nan","1,2,3,4,5x"};
    IndexedToneState before=state;
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        values[3].value.str=bad[i];
        CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG &&
            !memcmp(&before,&state,sizeof(state)));
    }
    IndexedToneState legacy={0};
    config.param_count=0;
    config.state_block=&legacy;
    CHECK(iface->create(&instance,&config)==ORPHEUS_OK && instance==&legacy);
    CHECK(iface->prepare(&legacy,&config)==ORPHEUS_OK && legacy.execution_mode==0);
    CHECK(iface->process(&legacy,&context)==ORPHEUS_OK && !memcmp(input,output,sizeof(input)));
    const char* legacy_ids[]={"gain_index","ramp_ms","filter_order","boost_coefs","cut_coefs"};
    OrpheusValue legacy_values[]={
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=255}, {.type=ORPHEUS_VALUE_FLOAT,.value.f32=1},
        {.type=ORPHEUS_VALUE_INT,.value.i32=2},
        {.type=ORPHEUS_VALUE_STRING,.value.str=".5,.25,.125,.1,.05"},
        {.type=ORPHEUS_VALUE_STRING,.value.str=".25,.1,.05,-.2,.1"}
    };
    config.param_ids=legacy_ids; config.param_values=legacy_values; config.param_count=5;
    CHECK(iface->prepare(&legacy,&config)==ORPHEUS_OK && legacy.algorithm.current_gain==10);
    float z1[2]={0}, z2[2]={0};
    for (unsigned block=0;block<36;++block) {
        if (block==1) {
            request.value.f32=0;
            CHECK(iface->set_parameter(&legacy,"gain_index",&request)==ORPHEUS_OK && legacy.algorithm.crossing_phase==1);
        }
        if (block==2 || block==3) {
            freeze.value.b=block==2;
            CHECK(iface->set_parameter(&legacy,"freeze",&freeze)==ORPHEUS_OK);
        }
        float current=legacy.algorithm.current_gain, expected[6];
        for (unsigned n=0;n<3;++n) {
            if (!legacy.freeze && legacy.algorithm.frame_count>0) current*=legacy.algorithm.ramp_coeff;
            const float* coefs=current>=1 ? legacy.algorithm.boost_coefs : legacy.algorithm.cut_coefs;
            for (unsigned c=0;c<2;++c) {
                float x=input[n*2+c], filtered=coefs[0]*x+z1[c];
                z1[c]=coefs[1]*x+z2[c]-coefs[3]*filtered;
                z2[c]=coefs[2]*x-coefs[4]*filtered;
                expected[n*2+c]=x+(current-1)*filtered;
            }
        }
        CHECK(iface->process(&legacy,&context)==ORPHEUS_OK && !memcmp(output,expected,sizeof(expected)));
    }
    CHECK(legacy.algorithm.crossing_phase==0 && legacy.algorithm.frame_count==0 &&
        legacy.algorithm.current_gain==legacy.algorithm.final_target_gain);
    request.value.f32=255;
    CHECK(iface->set_parameter(&legacy,"gain_index",&request)==ORPHEUS_OK);
    uint32_t remaining=legacy.algorithm.frame_count;
    float current=legacy.algorithm.current_gain;
    CHECK(remaining>1);
    context.frame_count=0;
    CHECK(iface->process(&legacy,&context)==ORPHEUS_OK &&
        legacy.algorithm.frame_count==remaining-1 && legacy.algorithm.current_gain==current);
    context.frame_count=3;
    before=legacy;in.frame_count=2;
    CHECK(iface->process(&legacy,&context)==ORPHEUS_ERR_INVALID_ARG &&
        !memcmp(&before,&legacy,sizeof(legacy)));
    in.frame_count=3;out.data=input+1;
    CHECK(iface->process(&legacy,&context)==ORPHEUS_ERR_INVALID_ARG &&
        !memcmp(&before,&legacy,sizeof(legacy)));
    out.data=output;
    unsigned calls=0;
    OrpheusContinuation continuation={.user=&calls,.emit=fail_delivery};
    context.continuation=&continuation;
    CHECK(iface->process(&legacy,&context)==ORPHEUS_ERR_PROCESSING && calls==1 &&
        legacy.algorithm.frame_count==before.algorithm.frame_count-1 &&
        memcmp(legacy.algorithm.z1,before.algorithm.z1,sizeof(legacy.algorithm.z1)));
    context.continuation=NULL;out.data=input;
    CHECK(iface->process(&legacy,&context)==ORPHEUS_OK);
    CHECK(iface->destroy(&state)==ORPHEUS_OK && iface->destroy(&legacy)==ORPHEUS_OK);
    return 0;
}
