#include "orpheus_indexed_tone.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
static int slots_seen;
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
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK);
    OrpheusRegistry registry={.add=check_slot};
    CHECK(iface->register_slots(&state,&registry)==ORPHEUS_OK && slots_seen==22);
    float input[]={1,-1,0.5f,-0.5f,0.25f,-0.25f}, output[6], z[2]={0};
    OrpheusBuffer in={.data=input,.channels=2,.format=ORPHEUS_FORMAT_F32,.frame_capacity=3};
    OrpheusBuffer out={.data=output,.channels=2,.format=ORPHEUS_FORMAT_F32,.frame_capacity=3};
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
    CHECK(state.phase==1 && state.ramps[0].remaining_blocks==0 && state.ramps[0].current_gain==gain);
    context.frame_count=0;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.phase==1);
    context.frame_count=3;
    OrpheusValue freeze={.type=ORPHEUS_VALUE_BOOL,.value.b=true};
    CHECK(iface->set_parameter(&state,"freeze",&freeze)==ORPHEUS_OK);
    float previous_z=state.z1[0];
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.phase==1 && state.z1[0]!=previous_z);
    freeze.value.b=false;
    CHECK(iface->set_parameter(&state,"freeze",&freeze)==ORPHEUS_OK);
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.phase==2 && state.ramps[1].remaining_blocks==0);
    CHECK(state.ramps[1].current_gain!=2);
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.ramps[1].current_gain==2);
    CHECK(iface->reset(&state)==ORPHEUS_OK && state.phase==2 && state.ramps[1].current_gain==2 && state.z1[0]==0);
    OrpheusValue request={.type=ORPHEUS_VALUE_FLOAT,.value.f32=128};
    CHECK(iface->set_parameter(&state,"gain_index",&request)==ORPHEUS_ERR_UNSUPPORTED);
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
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        values[3].value.str=bad[i];
        CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    }
    IndexedToneState legacy={0};
    config.param_count=0;
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
    CHECK(iface->prepare(&legacy,&config)==ORPHEUS_OK && legacy.current_gain==10);
    float z1[2]={0}, z2[2]={0};
    for (unsigned block=0;block<36;++block) {
        if (block==1) {
            request.value.f32=0;
            CHECK(iface->set_parameter(&legacy,"gain_index",&request)==ORPHEUS_OK && legacy.crossing_phase==1);
        }
        if (block==2 || block==3) {
            freeze.value.b=block==2;
            CHECK(iface->set_parameter(&legacy,"freeze",&freeze)==ORPHEUS_OK);
        }
        float current=legacy.current_gain, expected[6];
        for (unsigned n=0;n<3;++n) {
            if (!legacy.freeze && legacy.frame_count>0) current*=legacy.ramp_coeff;
            const float* coefs=current>=1 ? legacy.boost_coefs : legacy.cut_coefs;
            for (unsigned c=0;c<2;++c) {
                float x=input[n*2+c], filtered=coefs[0]*x+z1[c];
                z1[c]=coefs[1]*x+z2[c]-coefs[3]*filtered;
                z2[c]=coefs[2]*x-coefs[4]*filtered;
                expected[n*2+c]=x+(current-1)*filtered;
            }
        }
        CHECK(iface->process(&legacy,&context)==ORPHEUS_OK && !memcmp(output,expected,sizeof(expected)));
    }
    CHECK(legacy.crossing_phase==0 && legacy.frame_count==0 && legacy.current_gain==legacy.final_target_gain);
    return 0;
}
