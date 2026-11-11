#include "component_state.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
typedef orpheus_builtin_gain_ramper_State GainRamperState;
int main(void) {
    const OrpheusComponentInterface* iface=orpheus_get_interface();
    const char* ids[]={"execution_mode","channels","num_rampers","chan_map","current_gains","target_gains","coefficients","remaining_blocks"};
    OrpheusValue params[]={
        {.type=ORPHEUS_VALUE_INT,.value.i32=1}, {.type=ORPHEUS_VALUE_INT,.value.i32=4},
        {.type=ORPHEUS_VALUE_INT,.value.i32=3}, {.type=ORPHEUS_VALUE_STRING,.value.str="0,1,-1,2"},
        {.type=ORPHEUS_VALUE_STRING,.value.str=".8,.5,.75"}, {.type=ORPHEUS_VALUE_STRING,.value.str=".2,.3,.4"},
        {.type=ORPHEUS_VALUE_STRING,.value.str="1.01,1.02,.99"}, {.type=ORPHEUS_VALUE_STRING,.value.str="1,0,2"}
    };
    OrpheusConfig config={.sample_rate=48000,.block_size=4,.channels=4,.param_count=8,.param_ids=ids,.param_values=params};
    GainRamperState state={0};
    config.state_block=&state;
    void* instance=NULL;
    CHECK(iface->create(&instance,&config)==ORPHEUS_OK && instance==&state);
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK);
    uint32_t bits[]={0x80000000u,0x7fc12345u,0x7f800000u,0xff800000u};
    float input[16],output[16],expected[16], gains[]={.8f,.3f,.75f}, coefs[]={1.01f,1,.99f};
    for (unsigned n=0;n<4;++n) {
        for (unsigned r=0;r<3;++r) gains[r]*=coefs[r];
        for (unsigned c=0;c<4;++c) {
            unsigned i=n*4+c;
            memcpy(&input[i],&bits[(n+c)%4],4);
            if (c==2) memcpy(&expected[i],&input[i],4);
            else expected[i]=input[i]*gains[c==3 ? 2 : c];
        }
    }
    OrpheusBuffer in={.data=input,.channels=4,.frame_capacity=4,.frame_count=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=4,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=4};
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(output,expected,sizeof(output)));
    CHECK(state.algorithm.remaining_blocks[0]==0 && state.algorithm.remaining_blocks[1]==0 && state.algorithm.remaining_blocks[2]==1 &&
          !memcmp(state.algorithm.current_gains,gains,sizeof(gains)) && state.algorithm.current_gains[0]!=state.algorithm.target_gains[0]);
    context.frame_count=0;
    GainRamperState before=state;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(&before,&state,sizeof(state)));
    context.frame_count=4;
    OrpheusValue freeze={.type=ORPHEUS_VALUE_BOOL,.value.b=true};
    CHECK(iface->set_parameter(&state,"freeze",&freeze)==ORPHEUS_OK);
    before=state;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(&before,&state,sizeof(state)));
    CHECK(iface->reset(&state)==ORPHEUS_OK && !memcmp(&before,&state,sizeof(state)));
    state.algorithm.remaining_blocks[0]=-1;
    memcpy(expected,output,sizeof(expected));
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(expected,output,sizeof(expected)));
    params[7].value.str="-1,0,0";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    params[7].value.str="1,0,2"; params[1].value.i32=65;
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    params[1].value.i32=4;
    params[0].value.i32=2; params[3].value.str="0,1,-1,-2";
    params[4].value.str=".8,.5,.75"; params[7].value.str="1,0,-2147483648";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK && state.execution_mode==2);
    for (unsigned n=0;n<4;++n) {
        float frame_gains[]={.8f*powf(1.01f,(float)(n+1)),.3f,.75f*powf(.99f,(float)(n+1))};
        for (unsigned c=0;c<4;++c) {
            unsigned i=n*4+c;
            if (c==3) expected[i]=0;
            else if (c==2) memcpy(&expected[i],&input[i],4);
            else expected[i]=input[i]*frame_gains[c];
        }
    }
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(output,expected,sizeof(output)));
    CHECK(state.algorithm.current_gains[2]==.75f*powf(.99f,4) && state.algorithm.remaining_blocks[2]==INT32_MIN);
    params[7].value.str="2147483647,0,0";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK && state.algorithm.remaining_blocks[0]==INT32_MAX);
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.algorithm.remaining_blocks[0]==INT32_MAX-1);
    params[4].value.str="nan,1,1";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    params[4].value.str="1,1";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    config.param_count=1;config.param_ids=ids+1;config.param_values=params+1;
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK && state.execution_mode==0);
    OrpheusValue request={.type=ORPHEUS_VALUE_FLOAT,.value.f32=-6};
    CHECK(iface->set_parameter(&state,"gain_db",&request)==ORPHEUS_OK);
    float gain=state.algorithm.rampers[0].currentGain*expf(state.algorithm.rampers[0].rampCoeff);
    for (unsigned i=0;i<16;++i) input[i]=.01f*(i+1),expected[i]=input[i]*gain;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(output,expected,sizeof(output)));
    const char* mapping_ids[]={"channels","chan_map"};
    OrpheusValue mapping_params[]={
        {.type=ORPHEUS_VALUE_INT,.value.i32=4}, {.type=ORPHEUS_VALUE_STRING}
    };
    config.param_count=2;config.param_ids=mapping_ids;config.param_values=mapping_params;
    const char* invalid_maps[]={"x","0,1,2,0,1","1,","2147483648","1 trailing"};
    for(unsigned i=0;i<sizeof(invalid_maps)/sizeof(invalid_maps[0]);i++) {
        mapping_params[1].value.str=invalid_maps[i];before=state;
        CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    }
    mapping_params[1].value.str="0,-9999,2147483647";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK && state.algorithm.chanMap[0]==0 &&
          state.algorithm.chanMap[1]==-1 && state.algorithm.chanMap[2]==0 && state.algorithm.chanMap[3]==0);
    char long_map[1024];
    size_t position=0;
    for(unsigned c=0;c<64;c++) {
        int written=snprintf(long_map+position,sizeof(long_map)-position,"%s-2147483648",c ? "," : "");
        CHECK(written>0 && (size_t)written<sizeof(long_map)-position);
        position+=(size_t)written;
    }
    mapping_params[0].value.i32=64;mapping_params[1].value.str=long_map;
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK);
    for(unsigned c=0;c<64;c++) CHECK(state.algorithm.chanMap[c]==-1);
    CHECK(iface->destroy(&state)==ORPHEUS_OK);
    return 0;
}
