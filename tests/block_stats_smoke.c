#include "orpheus_level_detect.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    const OrpheusComponentInterface* iface=orpheus_get_interface();
    const char* ids[]={"execution_mode"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=1}};
    OrpheusConfig config={.sample_rate=48000,.block_size=4,.channels=2,.param_count=1,.param_ids=ids,.param_values=params};
    LevelDetectState state={0};
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK);
    float input[]={.5f,-.25f,.5f,-.25f,.5f,-.25f,.5f,-.25f},rms,peak;
    OrpheusBuffer in={.data=input,.channels=2,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32};
    OrpheusBuffer rms_out={.data=&rms,.channels=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32};
    OrpheusBuffer peak_out={.data=&peak,.channels=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32};
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={NULL,&rms_out,&peak_out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=3,.frame_count=4};
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && rms==log10f(.25f+state.db_epsilon)*10 &&
          peak==log10f(.5f+state.db_epsilon)*20 && rms_out.frame_count==1 && peak_out.frame_count==1);
    LevelDetectState before=state;
    context.frame_count=0;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(&before,&state,sizeof(state)));
    context.frame_count=4; rms_out.channels=2;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    rms_out.channels=1;
    CHECK(iface->reset(&state)==ORPHEUS_OK && state.rms_db==log10f(state.db_epsilon)*10);
    for (unsigned i=0;i<8;++i) input[i]=-0.0f;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && rms==log10f(state.db_epsilon)*10 &&
          peak==log10f(state.db_epsilon)*20);
    input[1]=NAN;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && isfinite(rms) && isfinite(peak));
    input[1]=0; input[2]=NAN;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && isnan(rms) && isfinite(peak));
    input[2]=0; input[0]=NAN;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && isnan(rms) && isnan(peak));
    input[0]=INFINITY;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && isinf(rms) && isinf(peak));
    before=state; in.data=NULL;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    in.data=input; in.frame_capacity=3;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    in.frame_capacity=4; context.output_count=0; context.outputs=NULL;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK);
    config.channels=33;
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    config.channels=2;
    config.param_count=0;
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK);
    for (unsigned i=0;i<8;++i) input[i]=i%2 ? -.25f : .5f;
    float output[8];
    OrpheusBuffer out={.data=output,.channels=2,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32};
    outputs[0]=&out; context.output_count=1; context.outputs=outputs;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(input,output,sizeof(input)) && state.level>0);
    state.mode=1;
    CHECK(iface->reset(&state)==ORPHEUS_OK);
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.level==state.attack_coeff*.5f);
    return 0;
}
