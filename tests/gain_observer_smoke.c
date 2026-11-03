#include "orpheus_gain_ramper.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    float input[96]={0},output[96]={0},observed=0;
    OrpheusBuffer in={.data=input,.channels=3,.frame_capacity=32,.format=ORPHEUS_FORMAT_F32};
    OrpheusBuffer out={.data=output,.channels=3,.frame_capacity=32,.format=ORPHEUS_FORMAT_F32};
    OrpheusBuffer scalar={.data=&observed,.channels=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32};
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={&out,&scalar};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=2,.frame_count=32};
    const char* ids[]={"execution_mode","num_rampers","chan_map","current_gains","target_gains",
                      "coefficients","remaining_blocks","gain_output","gain_channel"};
    OrpheusValue params[]={
        {.type=ORPHEUS_VALUE_INT}, {.type=ORPHEUS_VALUE_INT,.value.i32=1},
        {.type=ORPHEUS_VALUE_STRING}, {.type=ORPHEUS_VALUE_STRING,.value.str="0.04"},
        {.type=ORPHEUS_VALUE_STRING,.value.str="0.2"}, {.type=ORPHEUS_VALUE_STRING,.value.str="1.005"},
        {.type=ORPHEUS_VALUE_STRING,.value.str="2"}, {.type=ORPHEUS_VALUE_INT,.value.i32=1},
        {.type=ORPHEUS_VALUE_INT}
    };
    OrpheusConfig config={.sample_rate=48000,.block_size=32,.channels=3,.param_count=9,.param_ids=ids,.param_values=params};
    for (unsigned mode=0;mode<3;++mode) for (unsigned channel=0;channel<3;++channel) {
        GainRamperState state={0};
        params[0].value.i32=mode; params[2].value.str=mode==2 ? "0,-1,-2" : "0,-1,0";
        params[8].value.i32=channel;
        CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
        CHECK(api->process(&state,&context)==ORPHEUS_OK && scalar.frame_count==1);
        float expected=channel==1 ? 1 : channel==2 && mode==2 ? 0 :
                       mode ? state.current_gains[0] : state.rampers[0].currentGain;
        CHECK(!memcmp(&observed,&expected,4));
        GainRamperState snapshot=state; scalar.channels=2;
        CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&snapshot,&state,sizeof(state)));
        scalar.channels=1; outputs[1]=NULL;
        CHECK(api->process(&state,&context)==ORPHEUS_OK);
        outputs[1]=&scalar;
    }
    GainRamperState state={0}; params[8].value.i32=3;
    CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    return 0;
}
