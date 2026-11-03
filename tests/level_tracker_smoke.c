#include "orpheus_level_tracker.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    const OrpheusComponentInterface* iface=orpheus_get_interface();
    LevelTrackerState state={0};
    OrpheusConfig config={.sample_rate=48000,.block_size=32,.channels=1};
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK);
    OrpheusProcessContext context={.frame_count=32};
    state.hold_calls=1; state.hold_margin_db=3; state.slow_decay_db=-.5f; state.fast_decay_db=-2;
    state.rms_db=-20;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.envelope_db==-20 && state.holding);
    state.rms_db=-40;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.hold_count==1 && state.holding && state.envelope_db==-20.5f);
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.hold_count==2 && !state.holding && state.envelope_db==-22.5f);
    state.rms_db=-23;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.hold_count==0 && !state.holding && state.envelope_db==-24.5f);
    state.rms_db=-26; state.mode=-1;
    double before=state.hold_count;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.level_db_spl==0 && state.envelope_db==-24.5f && state.hold_count==before);
    state.mode=0; state.trigger=1; state.trigger_threshold=.5f; state.fast_multiplier=3;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.envelope_db==-30.5f);
    float input=-10,output=0;
    OrpheusBuffer scalar={.data=&input,.channels=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32};
    OrpheusBuffer scalar_out={.data=&output,.channels=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32};
    const OrpheusBuffer* inputs[]={&scalar,NULL,NULL,NULL,NULL}; OrpheusBuffer* outputs[]={&scalar_out};
    context.inputs=inputs; context.input_count=4; context.outputs=outputs; context.output_count=1;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.envelope_db==-10 && output==90 && scalar_out.frame_count==1);
    float volume=-7,trigger=0;
    OrpheusBuffer volume_in={.data=&volume,.channels=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32};
    OrpheusBuffer trigger_in={.data=&trigger,.channels=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32};
    inputs[2]=&volume_in; inputs[3]=&trigger_in;
    state.volume_db=9; input=-80;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.envelope_db==-10.5f && output==82.5f);
    state.mode=1; state.peak_db=-5;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.envelope_db==-5 && output==88);
    state.mode=0; state.volume_db=9; volume=.04f;
    inputs[2]=NULL; inputs[4]=&volume_in; context.input_count=5;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK &&
          output==fmaxf(0,(state.envelope_db+state.prediction_db_spl)+log10f(volume+state.volume_epsilon)*20));
    LevelTrackerState unit_snapshot=state; inputs[2]=&volume_in;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&unit_snapshot,&state,sizeof(state)));
    inputs[2]=&volume_in; inputs[4]=NULL; context.input_count=4;
    state.mode=0; state.envelope_db=-10; state.rms_db=-40;
    state.hold_count=16777216; state.hold_calls=16777216; state.holding=true;
    context.input_count=0; context.inputs=NULL; state.trigger=0;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.hold_count==16777217 && !state.holding);
    context.input_count=4; context.inputs=inputs;
    LevelTrackerState snapshot=state;
    context.frame_count=0;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(&snapshot,&state,sizeof(state)));
    context.frame_count=32; scalar_out.channels=2;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&snapshot,&state,sizeof(state)));
    scalar_out.channels=1; scalar.data=NULL;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&snapshot,&state,sizeof(state)));
    scalar.data=&input; input=NAN;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && isfinite(state.envelope_db));
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1};
    CHECK(iface->set_parameter(&state,"min_db",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=NAN;
    CHECK(iface->set_parameter(&state,"mode",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.type=ORPHEUS_VALUE_INT; value.value.i32=3;
    CHECK(iface->set_parameter(&state,"hold_calls",&value)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(iface->reset(&state)==ORPHEUS_OK && !state.initialized && state.holding && state.hold_count==0);
    return 0;
}
